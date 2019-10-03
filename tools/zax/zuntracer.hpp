// @HEADER_LANG C++ 
// @HEADER_COMPONENT zafl
// @HEADER_BEGIN
/*
* Copyright (c) 2018-2019 Zephyr Software LLC
*
* This file may be used and modified for non-commercial purposes as long as
* all copyright, permission, and nonwarranty notices are preserved.
* Redistribution is prohibited without prior written consent from Zephyr
* Software.
*
* Please contact the authors for restrictions applying to commercial use.
*
* THIS SOURCE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
* MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*
* Author: Zephyr Software
* e-mail: jwd@zephyr-software.com
* URL   : http://www.zephyr-software.com/
*
* This software was developed with SBIR funding and is subject to SBIR Data Rights, 
* as detailed below.
*
* SBIR DATA RIGHTS
*
* Contract No. __FA8750-17-C-0295___________________________.
* Contractor Name __Zephyr Software LLC_____________________.
* Address __4826 Stony Point Rd, Barboursville, VA 22923____.
*
*/

// @HEADER_END
#ifndef _LIBTRANSFORM_ZUNTRACER_H
#define _LIBTRANSFORM_ZUNTRACER_H

#include "zax.hpp"

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
