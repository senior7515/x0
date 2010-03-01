/* <x0/mod_debug.cpp>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 *
 * (c) 2009 Chrisitan Parpart <trapni@gentoo.org>
 */

#include <x0/server.hpp>
#include <x0/request.hpp>
#include <x0/response.hpp>
#include <x0/range_def.hpp>
#include <x0/strutils.hpp>
#include <x0/io/compress_filter.hpp>
#include <x0/types.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/bind.hpp>

#include <sstream>
#include <iostream>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/**
 * \ingroup plugins
 * \brief serves static files from server's local filesystem to client.
 */
class compress_plugin :
	public x0::plugin
{
private:
	x0::server::request_post_hook::connection post_process_;

public:
	compress_plugin(x0::server& srv, const std::string& name) :
		x0::plugin(srv, name)
	{
		post_process_ = server_.post_process.connect(boost::bind(&compress_plugin::post_process, this, _1, _2));
	}

	~compress_plugin() {
		server_.post_process.disconnect(post_process_);
	}

	virtual void configure()
	{
	}

private:
	void post_process(x0::request *in, x0::response *out)
	{
		if (x0::buffer_ref r = in->header("Accept-Encoding"))
		{
			typedef boost::tokenizer<boost::char_separator<char>> tokenizer;

			std::vector<std::string> items(x0::split<std::string>(r.str(), ", "));

			if (std::find(items.begin(), items.end(), "gzip") != items.end())
			{
				out->headers.set("Content-Encoding", "gzip");
				out->filter_chain.push_back(std::make_shared<x0::compress_filter>(/*gzip*/));
			}
			else if (std::find(items.begin(), items.end(), "deflate") != items.end())
			{
				out->headers.set("Content-Encoding", "deflate");
				out->filter_chain.push_back(std::make_shared<x0::compress_filter>(/*deflate*/));
			}
			else
				return;

			out->headers.set("Vary", "Accept-Encoding");

			//! \todo overwrite Content-Length to the actual (compressed) size, or use Chunked-Encoding.
			// this is a temporary fix to work around the missing bits above.
			out->headers.remove("Content-Length");
			out->headers.set("Connection", "closed");

			//! \todo cache compressed result if static file (maybe as part of compress_filter class?)

		}
	}
};

X0_EXPORT_PLUGIN(compress);
