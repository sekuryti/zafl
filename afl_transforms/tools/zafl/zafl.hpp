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
	Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false, bool p_verbose=false);
	int execute();
	void setWhitelist(const string&);
	void setBlacklist(const string&);
	void setBasicBlockOptimization(bool p_bb_graph_optimize) {m_bb_graph_optimize=p_bb_graph_optimize;}
	void setEnableForkServer(bool p_forkserver_enabled) {m_forkserver_enabled=p_forkserver_enabled;}

private:
	void afl_instrument_bb(Instruction_t *inst, const bool p_hasLeafAnnotation);
	void afl_instrument_bb1(Instruction_t *inst, const bool p_hasLeafAnnotation);
	void insertExitPoint(Instruction_t *inst);
	zafl_blockid_t get_blockid(const unsigned = 0xFFFF);
	void insertForkServer(Instruction_t* p_entry);
	void insertForkServer(string p_forkServerEntry);
	void setupForkServer();
	void insertExitPoints();
	bool isBlacklisted(const Function_t*) const;
	bool isWhitelisted(const Function_t*) const;
	bool isBlacklisted(const Instruction_t*) const;
	bool isWhitelisted(const Instruction_t*) const;

private:
	libIRDB::pqxxDB_t&           m_dbinterface;
	STARS::IRDB_Interface_t      m_stars_analysis_engine;

	string                       m_fork_server_entry;  // string to specify fork server entry point
	set<string>                  m_exitpoints;
	bool                         m_use_stars;
	bool                         m_autozafl;
	bool                         m_bb_graph_optimize;
	bool                         m_forkserver_enabled;
	bool                         m_verbose;

        std::pair<DataScoop_t*,int>  m_trace_map;  // afl shared memory trace map
        std::pair<DataScoop_t*,int>  m_prev_id;    // id of previous block
	Instruction_t*               m_plt_zafl_initAflForkServer; 

	std::set<std::string>        m_whitelist;  
	std::set<std::string>        m_blacklist;

	std::set<zafl_blockid_t>     m_used_blockid;

	unsigned m_num_flags_saved;
	unsigned m_num_temp_reg_saved;
	unsigned m_num_tracemap_reg_saved;
	unsigned m_num_previd_reg_saved;
};

} 

#endif
