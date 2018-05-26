#ifndef _LIBTRANSFORM_CONSTANT_UNROLL_H
#define _LIBTRANSFORM_CONSTANT_UNROLL_H

#include <libIRDB-core.hpp>
#include "transform.hpp"


namespace ConstantUnroll
{
// the actual transform.
class ConstantUnroll_t : public libTransform::Transform
{
public:
	// explicitly disable default and copy constructors
	ConstantUnroll_t() = delete;
	ConstantUnroll_t(const ConstantUnroll::ConstantUnroll_t&) = delete;
	ConstantUnroll_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();

private:
	libIRDB::pqxxDB_t &m_dbinterface;
	bool m_verbose;
};

} 

#endif
