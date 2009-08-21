/* <x0/mod_dirlisting.cpp>
 *
 * This file is part of the x0 web server project and is released under LGPL-3.
 *
 * (c) 2009 Chrisitan Parpart <trapni@gentoo.org>
 */

#include <x0/server.hpp>
#include <x0/request.hpp>
#include <x0/response.hpp>
#include <x0/header.hpp>
#include <x0/strutils.hpp>
#include <x0/types.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stddef.h>

/**
 * \ingroup modules
 * \brief implements automatic content generation for raw directories
 */
class dirlisting_plugin :
	public x0::plugin
{
private:
	x0::handler::connection c;

	struct context
	{
		bool enabled;
	};

public:
	dirlisting_plugin(x0::server& srv, const std::string& name) :
		x0::plugin(srv, name)
	{
		c = server_.generate_content.connect(boost::bind(&dirlisting_plugin::dirlisting, this, _1, _2));
	}

	~dirlisting_plugin()
	{
		server_.generate_content.disconnect(c);
	}

	virtual void configure()
	{
		auto hosts = server_.config()["Hosts"].keys<std::string>();
		for (auto i = hosts.begin(), e = hosts.end(); i != e; ++i)
		{
			bool enabled;

			if (server_.config()["Hosts"][*i]["DirectoryListing"].load(enabled)
			 || server_.config()["DirectoryListing"].load(enabled))
			{
				server_.create_context<context>(this, *i).enabled = enabled;
			}
		}
	}

private:
	bool enabled(x0::request& in)
	{
		try
		{
			return server_.context<context>(this, in.header("Host")).enabled;
		}
		catch (...)
		{
			return false;
		}
	}

	bool dirlisting(x0::request& in, x0::response& out)
	{
		if (!enabled(in))
			return false;

		if (!in.fileinfo->is_directory())
			return false;

		if (DIR *dir = opendir(in.fileinfo->filename().c_str()))
		{
			std::list<std::string> listing;
			listing.push_back("..");

			int len = offsetof(dirent, d_name) + pathconf(in.fileinfo->filename().c_str(), _PC_NAME_MAX);
			dirent *dep = (dirent *)new unsigned char[len + 1];
			dirent *res = 0;

			while (readdir_r(dir, dep, &res) == 0 && res)
			{
				std::string name(dep->d_name);

				if (name[0] != '.')
				{
					if (x0::fileinfo_ptr fi = in.connection.server().fileinfo(in.fileinfo->filename() + "/" + name))
					{
						if (fi->is_directory())
							name += "/";

						listing.push_back(name);
					}
				}
			}

			delete[] dep;

			std::stringstream sstr;
			sstr << "<html><head><title>Directory: "
				 << in.path
				 << "</title></head>\n<body>\n";

			sstr << "<h2>Index of " << in.path << "</h2>\n";
			sstr << "<ul>\n";

			for (std::list<std::string>::iterator i = listing.begin(), e = listing.end(); i != e; ++i)
			{
				sstr << "<li><a href='" << *i << "'>" << *i << "</a></li>\n";
			}

			sstr << "</ul>\n";

			sstr << "<hr/>\n";
			sstr << "<small><i>" << in.connection.server().tag() << "</i></small><br/>\n";

			sstr << "</body></html>\n";

			std::string result(sstr.str());

			out.write(result);
			out *= x0::header("Content-Type", "text/html");
			out *= x0::header("Content-Length", boost::lexical_cast<std::string>(result.size()));

			out.flush();

			closedir(dir);
			return true;
		}
		return false;
	}
};

X0_EXPORT_PLUGIN(dirlisting);
