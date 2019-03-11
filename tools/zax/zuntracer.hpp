#ifndef _LIBTRANSFORM_ZUNTRACER_H
#define _LIBTRANSFORM_ZUNTRACER_H

#include "zax_base.hpp"

namespace Zafl
{

	using namespace IRDB_SDK;

	// Block-level instrumentation for Untracer
	class ZUntracer_t : public ZaxBase_t
	{
		public:
			ZUntracer_t() = delete;
			ZUntracer_t(const ZUntracer_t&) = delete;
			ZUntracer_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false);
			virtual ~ZUntracer_t() {};

		protected:
			virtual void instrumentBasicBlock(BasicBlock_t*, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false);
			virtual set<BasicBlock_t*> getBlocksToInstrument(ControlFlowGraph_t&);

		private:
			void _instrumentBasicBlock_fixed(BasicBlock_t*, char* p_tracemap_addr);
			void _instrumentBasicBlock(BasicBlock_t*, const bool p_redZoneHint);
	};

} 

#endif