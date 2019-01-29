#ifndef _LIBTRANSFORM_CONSTANT_DECOMPOSE_H
#define _LIBTRANSFORM_CONSTANT_DECOMPOSE_H

#include <irdb-core>
#include <transform.hpp>


namespace ConstantDecompose
{
// the actual transform.
class ConstantDecompose_t : public libTransform::Transform
{
public:
	// explicitly disable default and copy constructors
	ConstantDecompose_t() = delete;
	ConstantDecompose_t(const ConstantDecompose::ConstantDecompose_t&) = delete;
	ConstantDecompose_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();

private:
	IRDB_SDK::pqxxDB_t &m_dbinterface;
	bool m_verbose;
};

} 

#endif
