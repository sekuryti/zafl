#ifndef _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H
#define _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H

#include <irdb-core>

namespace Zafl
{
	using namespace IRDB_SDK;

	//
	// Break critical edges
	//
	class CriticalEdgeBreaker_t
	{
		public:
			CriticalEdgeBreaker_t(FileIR_t *p_variantIR, const bool p_verbose=false);
			unsigned getNumberExtraNodes() const;

		protected:
			void breakCriticalEdges();

		private:
			unsigned breakCriticalEdges(Function_t*);

		private:
			FileIR_t*    m_IR;
			const bool   m_verbose;
			unsigned     m_extra_nodes;
	};
} 

#endif
