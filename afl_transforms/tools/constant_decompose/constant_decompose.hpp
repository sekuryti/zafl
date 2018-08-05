#ifndef _LIBTRANSFORM_CONSTANT_DECOMPOSE_H
#define _LIBTRANSFORM_CONSTANT_DECOMPOSE_H

#include <libIRDB-core.hpp>
#include "transform.hpp"


namespace ConstantDecompose
{
// the actual transform.
class ConstantDecompose_t : public libTransform::Transform
{
public:
	// explicitly disable default and copy constructors
	ConstantDecompose_t() = delete;
	ConstantDecompose_t(const ConstantDecompose::ConstantDecompose_t&) = delete;
	ConstantDecompose_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();

private:
	libIRDB::pqxxDB_t &m_dbinterface;
	bool m_verbose;
};

} 

#endif
