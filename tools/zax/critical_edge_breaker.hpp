#ifndef _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H
#define _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H

#include <irdb-core>
#include "zax.hpp"

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
