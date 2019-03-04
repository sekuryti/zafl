#ifndef _ZAFL_LAF_H
#define _ZAFL_LAF_H

#include <irdb-core>
#include <irdb-transform>

namespace Laf
{
// the actual transform.
class Laf_t : public IRDB_SDK::Transform
{
public:
	// explicitly disable default and copy constructors
	Laf_t() = delete;
	Laf_t(const Laf::Laf_t&) = delete;
	Laf_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();

private:
	IRDB_SDK::pqxxDB_t &m_dbinterface;
	bool m_verbose;
};

} 

#endif
