#ifndef sw_x0_io_file_source_hpp
#define sw_x0_io_file_source_hpp 1

#include <x0/fd_source.hpp>
#include <string>

namespace x0 {

//! \addtogroup io
//@{

/** file source.
 */
class X0_API file_source :
	public fd_source
{
public:
	explicit file_source(const std::string& filename);
	~file_source();
};

//@}

} // namespace x0

#endif
