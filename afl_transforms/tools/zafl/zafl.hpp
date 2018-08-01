#ifndef _LIBTRANSFORM_ZAFL_H
#define _LIBTRANSFORM_ZAFL_H

#include <libIRDB-core.hpp>
#include <stars.h>
#include "transform.hpp"

namespace Zafl
{
typedef unsigned zafl_blockid_t;

// the actual transform.
class Zafl_t : public libTransform::Transform
{
public:
	// explicitly disable default and copy constructors
	Zafl_t() = delete;
	Zafl_t(const Zafl::Zafl_t&) = delete;
	Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_verbose=false);
	int execute();

	void afl_instrument_bb(Instruction_t *inst, const bool p_hasLeafAnnotation);
private:
	zafl_blockid_t get_blockid(const unsigned = 0xFFFF);
private:
	libIRDB::pqxxDB_t &m_dbinterface;
	STARS::IRDB_Interface_t m_stars_analysis_engine;
	bool m_verbose;

        std::pair<DataScoop_t*, int> m_trace_map; // afl shared memory trace map
        std::pair<DataScoop_t*, int> m_prev_id;    // id of previous block

	std::set<zafl_blockid_t> m_used_blockid;
	std::set<std::string> m_blacklistedFunctions;
};

} 

#endif
