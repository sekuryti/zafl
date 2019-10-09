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
#ifndef _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H
#define _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H

#include <irdb-core>
#include "zax.hpp"
#include <fstream>

namespace Zafl
{
	using namespace IRDB_SDK;

	//
	// Break critical edges
	//
	class CriticalEdgeBreaker_t
	{
		public:
			CriticalEdgeBreaker_t(FileIR_t *p_variantIR, set<string> p_blacklist=set<string>(), const bceStyle_t=bceAll, const bool p_verbose=false);
			unsigned getNumberExtraNodes() const;

		protected:
			void breakCriticalEdges();

		private:
			unsigned breakCriticalEdges(Function_t*);

		private:
			FileIR_t*          m_IR;
			const set<string>  m_blacklist;       
			const bool         m_verbose;
			unsigned           m_extra_nodes;
			const bceStyle_t   m_break_style;
			ofstream           map_file;
	};
} 

#endif
