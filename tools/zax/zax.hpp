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
#ifndef _LIBTRANSFORM_ZAX_H
#define _LIBTRANSFORM_ZAX_H

namespace Zafl
{
	enum bceStyle_t  { bceAll, bceNone, bceTargets, bceFallthroughs};
}
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
		virtual void instrumentBasicBlock(BasicBlock_t *p_bb, bool p_honor_red_zone, const bool p_collafl_optimization=false);

	private:
		set<ZaflBlockId_t>   m_used_blockid;  // internal bookkeeping to keep track of used block ids
	};

} 

#endif
