#ifndef _LIBTRANSFORM_ZAX_H
#define _LIBTRANSFORM_ZAX_H

#include "zax_base.hpp"

namespace Zafl
{
	//
	// Implements afl-style edge coverage instrumentation
	//
	class Zax_t : public ZaxBase_t
	{
	public:
		// explicitly disable default and copy constructors
		Zax_t() = delete;
		Zax_t(const Zafl::Zax_t&) = delete;
		Zax_t(pqxxDB_t &p_dbinterface, FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false);
		virtual ~Zax_t() {};

	protected:
		virtual ZaflBlockId_t getBlockId(const unsigned p_maxid=0xFFFF);
		virtual void instrumentBasicBlock(BasicBlock_t *p_bb, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false);

	private:
		set<ZaflBlockId_t>   m_used_blockid;  // internal bookkeeping to keep track of used block ids
	};

} 

#endif
