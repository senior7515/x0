/* <src/HttpWorker.cpp>
 *
 * This file is part of the x0 web server project and is released under AGPL-3.
 * http://www.xzero.io/
 *
 * (c) 2009-2014 Christian Parpart <trapni@gmail.com>
 */

#include <x0/http/HttpWorker.h>
#include <x0/http/HttpRequest.h>
#include <x0/http/HttpServer.h>
#include <x0/http/HttpConnection.h>
#include <x0/ServerSocket.h>
#include <x0/DebugLogger.h>
#include <x0/sysconfig.h>

#include <algorithm>
#include <cstdarg>
#include <ev++.h>
#include <signal.h>
#include <pthread.h>

// XXX one a connection has been passed to a worker, it is *bound* to it.

#if 0//!defined(XZERO_NDEBUG)
//#	define TRACE(n, msg...) X0_DEBUG("worker", (n), msg)
#	define TRACE(n, msg...) log(Severity::debug ## n, msg)
#else
#	define TRACE(n, msg...) do {} while (0)
#endif

namespace x0 {

/*!
 * Creates an HTTP worker instance.
 *
 * \param server   the worker parents server instance
 * \param loop     the event loop to be used within this worker
 * \param id       unique ID within the server instance
 * \param threaded whether or not to spawn a thread to actually run this worker
 */
HttpWorker::HttpWorker(HttpServer& server, struct ev_loop *loop, unsigned int id, bool threaded) :
	id_(id),
	state_(Inactive),
	server_(server),
	loop_(loop),
	startupTime_(ev_now(loop_)),
	now_(),
	connectionLoad_(0),
	requestCount_(0),
	connectionCount_(0),
	thread_(pthread_self()),
	queue_(),
	resumeLock_(),
	resumeCondition_(),
	performanceCounter_(),
	stopHandler_(),
	killHandler_(),
	connections_(nullptr),
	freeConnections_(nullptr),
	evLoopCheck_(loop_),
	evNewConnection_(loop_),
	evWakeup_(loop_),
#if !defined(X0_WORKER_POST_LIBEV)
	postLock_(),
	postQueue_(),
#endif
	fileinfo(loop_, &server_.fileinfoConfig_)
{
#if !defined(X0_WORKER_POST_LIBEV)
	pthread_mutex_init(&postLock_, nullptr);
#endif

	evLoopCheck_.set<HttpWorker, &HttpWorker::onLoopCheck>(this);
	evLoopCheck_.start();

	evNewConnection_.set<HttpWorker, &HttpWorker::onNewConnection>(this);
	evNewConnection_.start();

	evWakeup_.set<HttpWorker, &HttpWorker::onWakeup>(this);
	evWakeup_.start();

	pthread_mutex_init(&resumeLock_, nullptr);
	pthread_cond_init(&resumeCondition_, nullptr);

	if (threaded) {
		pthread_create(&thread_, nullptr, &HttpWorker::_run, this);
	}

	setName("xzero-io/%d", id_);

	TRACE(1, "spawned");
}

HttpWorker::~HttpWorker()
{
	TRACE(1, "destroying");

	clearCustomData();

#if !defined(X0_WORKER_POST_LIBEV)
	pthread_mutex_destroy(&postLock_);
#endif

	pthread_cond_destroy(&resumeCondition_);
	pthread_mutex_destroy(&resumeLock_);

	evLoopCheck_.stop();
	evNewConnection_.stop();
	evWakeup_.stop();

	freeCache();
}

void* HttpWorker::_run(void* p)
{
	reinterpret_cast<HttpWorker*>(p)->run();
	return nullptr;
}

void HttpWorker::run()
{
	state_ = Running;

	// XXX invoke onWorkerSpawned-hook here because we want to ensure this hook is 
	// XXX being invoked from *within* the worker-thread.
	server_.onWorkerSpawn(this);

	TRACE(1, "enter loop");
	ev_loop(loop_, 0);

	while (connections_)
		_kill();

	server_.onWorkerUnspawn(this);

	state_ = Inactive;
}

/*!
 * Sets the thread/process name that's running this worker.
 */
void HttpWorker::setName(const char* fmt, ...)
{
	char buf[17]; // the name may be at most 16 bytes long.
	va_list va;

	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

#if defined(HAVE_PTHREAD_SETNAME_NP)
	pthread_setname_np(thread_, buf);
#endif
}

void HttpWorker::log(LogMessage&& msg)
{
	msg.addTag("worker/%u", id());

	server().log(std::forward<LogMessage>(msg));
}

/** enqueues/assigns/registers given client connection information to this worker.
 */
void HttpWorker::enqueue(std::pair<Socket*, ServerSocket*>&& client)
{
	queue_.enqueue(client);
	evNewConnection_.send();
}

/** callback to be invoked when new connection(s) have been assigned to this worker.
 */
void HttpWorker::onNewConnection(ev::async& /*w*/, int /*revents*/)
{
	std::pair<Socket*, ServerSocket*> client;

	while (queue_.dequeue(&client)) {
		spawnConnection(client.first, client.second);
	}
}

void HttpWorker::onWakeup(ev::async& w, int revents)
{
	std::function<void()> fn;

	while (true) {
		pthread_mutex_lock(&postLock_);
		if (postQueue_.empty())
			goto out;

		fn = postQueue_.front();
		postQueue_.pop_front();
		pthread_mutex_unlock(&postLock_);

		if (fn) {
			fn();
		}
	}

out:
	pthread_mutex_unlock(&postLock_);
}

void HttpWorker::spawnConnection(Socket* client, ServerSocket* listener)
{
	TRACE(1, "client connected; fd:%d", client->handle());

	++connectionLoad_;
	++connectionCount_;

	// XXX since socket has not been used so far, I might be able to defer its creation out of its socket descriptor
	// XXX so that I do not have to double-initialize libev's loop handles for this socket.
	client->setLoop(loop_);

	HttpConnection* c;
	if (likely(freeConnections_ != nullptr)) {
		c = freeConnections_;
		c->id_ = connectionCount_;
		freeConnections_ = c->next_;

		c->reinitialize();
	}
	else {
		c = new HttpConnection(this, connectionCount_/*id*/);
	}

	if (connections_)
		connections_->prev_ = c;

	c->next_ = connections_;
	connections_ = c;

	c->start(listener, client);
}

/** releases/unregisters given (and to-be-destroyed) connection from this worker.
 *
 * This decrements the connection-load counter by one.
 */
void HttpWorker::release(HttpConnection* c)
{
	//    /--<--\   /--<--\   /--<--\
	// NULL     item1     item2     item3     NULL
	//              \-->--/   \-->--/   \-->--/

	--connectionLoad_;

	// unlink from active-list
	HttpConnection* prev = c->prev_;
	HttpConnection* next = c->next_;

	if (prev)
		prev->next_ = next;

	if (next)
		next->prev_ = prev;

	if (c == connections_)
		connections_ = next;

	// link into free-list
	c->next_ = freeConnections_;
	c->prev_ = nullptr;					// not needed
	if (freeConnections_ && freeConnections_->prev_)
		freeConnections_->prev_ = c;	// not needed
	freeConnections_ = c;
}

/**
 * Clear some cached objects to free up memory.
 */
void HttpWorker::freeCache()
{
	size_t i = 0;
	while (freeConnections_) {
		auto next = freeConnections_->next_;
		delete freeConnections_;
		freeConnections_ = next;
		++i;
	}
	TRACE(1, "cleared %zu free-connections items", i);
}

void HttpWorker::handleRequest(HttpRequest *r)
{
	++requestCount_;
	performanceCounter_.touch(now_.value());

	BufferRef expectHeader = r->requestHeader("Expect");
	bool contentRequired = r->method == "POST" || r->method == "PUT";

	if (contentRequired) {
		if (r->connection.contentLength() == -1 && !r->connection.isChunked()) {
			r->status = HttpStatus::LengthRequired;
			r->finish();
			return;
		}
	} else {
		if (r->contentAvailable()) {
			r->status = HttpStatus::BadRequest; // FIXME do we have a better status code?
			r->finish();
			return;
		}
	}

	if (expectHeader) {
		r->expectingContinue = equals(expectHeader, "100-continue");

		if (!r->expectingContinue || !r->supportsProtocol(1, 1)) {
			r->status = HttpStatus::ExpectationFailed;
			r->finish();
			return;
		}
	}

	server_.onPreProcess(r);

	if (!server_.requestHandler(r))
		r->finish();
}

void HttpWorker::_stop()
{
	TRACE(1, "_stop");

	evLoopCheck_.stop();
	evNewConnection_.stop();
	evWakeup_.stop();

	for (auto handler: stopHandler_)
		handler();
}

void HttpWorker::onLoopCheck(ev::check& /*w*/, int /*revents*/)
{
	// update server time
	now_.update(ev_now(loop_));
}

void HttpWorker::setAffinity(int cpu)
{
#ifdef HAVE_PTHREAD_SETAFFINITY_NP
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);

	TRACE(1, "setAffinity: %d", cpu);

	int rv = pthread_setaffinity_np(thread_, sizeof(set), &set);
	if (rv < 0) {
		log(Severity::error, "setting scheduler affinity on CPU %d failed for worker %u. %s", cpu, id_, strerror(errno));
	}
#else
	log(Severity::error, "setting scheduler affinity on CPU %d failed for worker %u. %s", cpu, id_, strerror(ENOTSUP));
#endif
}

void HttpWorker::bind(ServerSocket* s)
{
	s->set<HttpWorker, &HttpWorker::spawnConnection>(this);
}

/** suspend the execution of the worker thread until resume() is invoked.
 *
 * \note has no effect on main worker
 * \see resume()
 */
void HttpWorker::suspend()
{
	TRACE(1, "suspend");

	if (id_ != 0)
		post<HttpWorker, &HttpWorker::_suspend>(this);
}

void HttpWorker::_suspend()
{
	TRACE(1, "_suspend");
	pthread_mutex_lock(&resumeLock_);
	state_ = Suspended;
	pthread_cond_wait(&resumeCondition_, &resumeLock_);
	state_ = Running;
	pthread_mutex_unlock(&resumeLock_);
}

/** resumes the previousely suspended worker thread.
 *
 * \note has no effect on main worker
 * \see suspend()
 */
void HttpWorker::resume()
{
	TRACE(1, "resume");
	if (id_ != 0)
		pthread_cond_signal(&resumeCondition_);
}

void HttpWorker::stop()
{
	TRACE(1, "stop: post -> _stop() (while in state: %d)", state_);

	if (state_ != Running)
		return;

//	fileinfo.stop();

	post<HttpWorker, &HttpWorker::_stop>(this);
}

void HttpWorker::join()
{
	if (thread_ != pthread_self()) {
		pthread_join(thread_, nullptr);
	}
}

/*! Actually aborts all active connections.
 *
 * \see HttpConnection::abort()
 */
void HttpWorker::kill()
{
	TRACE(1, "kill: post -> _kill()");
	post<HttpWorker, &HttpWorker::_kill>(this);
}

/*! Actually aborts all active connections.
 *
 * \note Must be invoked from within the worker's thread.
 *
 * \see HttpConnection::abort()
 */
void HttpWorker::_kill()
{
	TRACE(1, "_kill()");
	while (connections_) {
		std::list<HttpConnection*> copy;

		for (HttpConnection* c = connections_; c != nullptr; c = c->next_)
			copy.push_back(c);

		for (auto c: copy)
			c->abort();

#ifndef XZERO_NDEBUG
		for (HttpConnection* c = connections_; c != nullptr; c = c->next_)
			c->log(Severity::debug, "connection still open");
#endif
	}

	for (auto handler: killHandler_) {
		TRACE(1, "_kill: invoke kill handler");
		handler();
	}
}

std::list<std::function<void()>>::iterator HttpWorker::registerStopHandler(std::function<void()> callback)
{
	stopHandler_.push_front(callback);
	return stopHandler_.begin();
}

void HttpWorker::unregisterStopHandler(std::list<std::function<void()>>::iterator handle)
{
	stopHandler_.erase(handle);
}

std::list<std::function<void()>>::iterator HttpWorker::registerKillHandler(std::function<void()> callback)
{
	killHandler_.push_front(callback);
	return killHandler_.begin();
}

void HttpWorker::unregisterKillHandler(std::list<std::function<void()>>::iterator handle)
{
	killHandler_.erase(handle);
}

void HttpWorker::post_thunk3(int revents, void* arg)
{
	std::function<void()>* callback = static_cast<std::function<void()>*>(arg);
	(*callback)();
	delete callback;
}

} // namespace x0
