#ifndef sw_x0_io_socket_sink_hpp
#define sw_x0_io_socket_sink_hpp 1

#include <x0/io/fd_sink.hpp>
#include <x0/io/source_visitor.hpp>
#include <x0/io/buffer.hpp>
#include <x0/types.hpp>

namespace x0 {

//! \addtogroup io
//@{

/** file descriptor stream sink.
 */
class X0_API socket_sink :
	public fd_sink,
	public source_visitor
{
public:
	explicit socket_sink(tcp_socket& sock);

	tcp_socket& socket() const;

	template<typename CompletionHandler>
	void on_ready(CompletionHandler handler);

	virtual ssize_t pump(source& src);

public:
	virtual void visit(fd_source& v);
	virtual void visit(file_source& v);
	virtual void visit(buffer_source& v);
	virtual void visit(filter_source& v);
	virtual void visit(composite_source& v);

protected:
	tcp_socket& socket_;
	ssize_t rv_;
};

//@}

// {{{ impl
inline socket_sink::socket_sink(tcp_socket& sock) :
	fd_sink(sock.native()),
	socket_(sock),
	rv_()
{
}

inline tcp_socket& socket_sink::socket() const
{
	return socket_;
}

template<typename CompletionHandler>
inline void socket_sink::on_ready(CompletionHandler handler)
{
	socket_.async_write_some(asio::null_buffers(), handler);
}
// }}}

} // namespace x0

#endif
