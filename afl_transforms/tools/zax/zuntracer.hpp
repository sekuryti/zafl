#ifndef _LIBTRANSFORM_ZUNTRACER_H
#define _LIBTRANSFORM_ZUNTRACER_H

#include "zax.hpp"

namespace Zafl
{

// Block-level instrumentation for Untracer
class ZUntracer_t : public Zax_t
{
public:
	ZUntracer_t() = delete;
	ZUntracer_t(const ZUntracer_t&) = delete;
	ZUntracer_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false, bool p_verbose=false);
	virtual ~ZUntracer_t() {};
	virtual int execute();

protected:
	virtual zafl_blockid_t get_blockid(const unsigned p_maxid = 0xFFFF);
	virtual void afl_instrument_bb(Instruction_t *p_inst, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false);

private:
	zafl_blockid_t   m_blockid;
};

} 

#endif
