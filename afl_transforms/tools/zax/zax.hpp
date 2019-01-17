#ifndef _LIBTRANSFORM_ZAX_H
#define _LIBTRANSFORM_ZAX_H

#include <libIRDB-core.hpp>
#include <stars.h>
#include "transform.hpp"

// utility functions
// @todo: move these functions into other libs for reuse
extern void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i);
extern RegisterSet_t get_dead_regs(Instruction_t* insn, MEDS_AnnotationParser &meds_ap_param);
extern RegisterSet_t get_free_regs(const RegisterSet_t candidates, const RegisterSet_t allowed);

namespace Zafl
{
typedef unsigned zafl_blockid_t;
typedef unsigned zafl_labelid_t;
typedef vector<Instruction_t*> BBRecord_t;

//
// Transform to add afl-compatible instrumentation, including a fork server
//
class Zax_t : public libTransform::Transform
{
public:
	// explicitly disable default and copy constructors
	Zax_t() = delete;
	Zax_t(const Zafl::Zax_t&) = delete;
	Zax_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false, bool p_verbose=false);
	virtual ~Zax_t() {};
	virtual int execute();
	void setWhitelist(const string& p_filename); 
	void setBlacklist(const string& p_filename); 
	void setBasicBlockOptimization(bool p_bb_graph_optimize) {m_bb_graph_optimize=p_bb_graph_optimize;}
	void setEnableForkServer(bool p_forkserver_enabled) {m_forkserver_enabled=p_forkserver_enabled;}
	void setBreakupCriticalEdges(const bool p_breakupCriticalEdges);

protected:
	virtual zafl_blockid_t get_blockid(const unsigned p_maxid=0xFFFF);
	virtual zafl_labelid_t get_labelid(const unsigned p_maxid=0xFFFF);
	virtual void afl_instrument_bb(Instruction_t *inst, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false);
	void insertExitPoint(Instruction_t *inst);
	void insertForkServer(Instruction_t* p_entry);
	void insertForkServer(string p_forkServerEntry);
	void setupForkServer();
	void insertExitPoints();
	bool isBlacklisted(const Function_t*) const;
	bool isWhitelisted(const Function_t*) const;
	bool isBlacklisted(const Instruction_t*) const;
	bool isWhitelisted(const Instruction_t*) const;
	virtual void setup();
	virtual void teardown();
	virtual void dump_map();
	virtual void dump_attributes();

protected:
	libIRDB::pqxxDB_t&           m_dbinterface;
	STARS::IRDB_Interface_t      m_stars_analysis_engine;

	string                       m_fork_server_entry;  // string to specify fork server entry point
	set<string>                  m_exitpoints;         // set of strings to specify exit points
	bool                         m_use_stars;          // use STARS to have access to dead register info
	bool                         m_autozafl;           // link in library w/ auto fork server
	bool                         m_bb_graph_optimize;  // skip basic blocks based on graph
	bool                         m_forkserver_enabled; // fork server enabled?
	bool                         m_breakupCriticalEdges;
	bool                         m_verbose;

        std::pair<DataScoop_t*,int>  m_trace_map;  // afl shared memory trace map
        std::pair<DataScoop_t*,int>  m_prev_id;    // id of previous block
	Instruction_t*               m_plt_zafl_initAflForkServer; // plt entry for afl fork server initialization routine

	std::set<std::string>        m_whitelist;   // whitelisted functions and/or instructions
	std::set<std::string>        m_blacklist;   // blacklisted functions and/or instructions

	zafl_labelid_t               m_labelid;     // internal bookkeeping to generate labels

	map<zafl_blockid_t, BBRecord_t> m_modifiedBlocks;  // keep track of modified blocks

	// stats
	unsigned m_num_bb;
	unsigned m_num_bb_instrumented;
	unsigned m_num_bb_skipped;
	unsigned m_num_bb_skipped_pushjmp;
	unsigned m_num_bb_skipped_nop_padding;

private:
	std::set<zafl_blockid_t>     m_used_blockid;      // internal bookkeeping to keep track of used block ids
};

} 

#endif
