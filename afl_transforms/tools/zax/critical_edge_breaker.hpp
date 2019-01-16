#ifndef _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H
#define _LIBTRANSFORM_CRITICAL_EDGE_BREAKER_H

#include <libIRDB-core.hpp>

namespace Zafl
{
//
// Break critical edges
//
class CriticalEdgeBreaker_t
{
  public:
	// explicitly disable default and copy constructors
	CriticalEdgeBreaker_t(libIRDB::FileIR_t *p_variantIR, const bool p_verbose=false);
	unsigned getNumberExtraNodes() const;

  protected:
	void breakCriticalEdges();

  private:
	unsigned breakCriticalEdges(libIRDB::Function_t*);

  private:
	libIRDB::FileIR_t*  m_IR;
	const bool          m_verbose;
	unsigned            m_extra_nodes;
};


} 

#endif
