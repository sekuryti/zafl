#ifndef _LIBTRANSFORM_ZAXBASE_H
#define _LIBTRANSFORM_ZAXBASE_H

#include <irdb-core>
#include <irdb-cfg>
#include <irdb-transform>
#include <stars.h>
#include <MEDS_Register.hpp>


// utility functions
// @todo: move these functions into other libs for reuse
extern void create_got_reloc(IRDB_SDK::FileIR_t* fir, std::pair<IRDB_SDK::DataScoop_t*,int> wrt, IRDB_SDK::Instruction_t* i);
extern MEDS_Annotation::RegisterSet_t get_dead_regs(IRDB_SDK::Instruction_t* insn, MEDS_Annotation::MEDS_AnnotationParser &meds_ap_param);
extern MEDS_Annotation::RegisterSet_t get_free_regs(const MEDS_Annotation::RegisterSet_t candidates, const MEDS_Annotation::RegisterSet_t allowed);

namespace Zafl
{
	using namespace IRDB_SDK;
	using namespace std;
	using zafl_blockid_t = unsigned;
	using zafl_labelid_t = unsigned;
	using BBRecord_t = vector<Instruction_t*>;

	/*
	 * Base class for afl-compatible instrumentation:
	 *   - fork server
	 *   - trace map 
	 */
	class ZaxBase_t : public Transform
	{
		public:
			ZaxBase_t() = delete;
			ZaxBase_t(const Zafl::ZaxBase_t&) = delete;
			virtual ~ZaxBase_t() {};
			virtual int execute();
			void setWhitelist(const string& p_filename); 
			void setBlacklist(const string& p_filename); 
			void setVerbose(bool); 
			void setBasicBlockOptimization(bool);
			void setBasicBlockFloatingInstrumentation(bool);
			void setEnableForkServer(bool);
			void setBreakupCriticalEdges(bool);

		protected:
			ZaxBase_t(pqxxDB_t &p_dbinterface, FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false);

			virtual void afl_instrument_bb(BasicBlock_t *p_bb, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false) = 0;

			virtual zafl_blockid_t get_blockid(const unsigned p_maxid=0xFFFF);
			virtual zafl_labelid_t get_labelid(const unsigned p_maxid=0xFFFF);
			virtual set<BasicBlock_t*> getBlocksToInstrument(ControlFlowGraph_t &);
			virtual Instruction_t* getInstructionToInstrument(const BasicBlock_t *, const unsigned p_num_free_regs_desired = 0);
			virtual void setup();
			virtual void teardown();
			virtual void dumpAttributes();
			virtual void dumpMap();

			void insertExitPoint(Instruction_t *inst);
			void insertForkServer(Instruction_t* p_entry);
			void insertForkServer(string p_forkServerEntry);
			void setupForkServer();
			void insertExitPoints();
			bool isBlacklisted(const Function_t*) const;
			bool isWhitelisted(const Function_t*) const;
			bool isBlacklisted(const Instruction_t*) const;
			bool isWhitelisted(const Instruction_t*) const;
			bool BB_isPushJmp(const BasicBlock_t *) const;
			bool BB_isPaddingNop(const BasicBlock_t *) const;

			bool getBasicBlockFloatingInstrumentation() const;

		protected:
			pqxxDB_t&                 m_dbinterface;
			STARS::IRDB_Interface_t   m_stars_analysis_engine;

			bool                      m_use_stars;          // use STARS to have access to dead register info
			bool                      m_autozafl;           // link in library w/ auto fork server
			bool                      m_bb_graph_optimize;  // skip basic blocks based on graph
			bool                      m_forkserver_enabled; // fork server enabled?
			bool                      m_breakupCriticalEdges;
			bool                      m_bb_float_instrumentation;  // skip basic blocks based on graph
			bool                      m_verbose;

			pair<DataScoop_t*,int>    m_trace_map;  // afl shared memory trace map
			pair<DataScoop_t*,int>    m_prev_id;    // id of previous block
			Instruction_t*            m_plt_zafl_initAflForkServer; // plt entry for afl fork server initialization routine

			map<zafl_blockid_t, BBRecord_t> m_modifiedBlocks;  // keep track of modified blocks

			// stats
			unsigned m_num_bb;
			unsigned m_num_bb_instrumented;
			unsigned m_num_bb_skipped;
			unsigned m_num_bb_skipped_pushjmp;
			unsigned m_num_bb_skipped_nop_padding;
			unsigned m_num_bb_skipped_innernode;
			unsigned m_num_bb_skipped_cbranch;
			unsigned m_num_bb_skipped_onlychild;
			unsigned m_num_bb_keep_exit_block;
			unsigned m_num_bb_keep_cbranch_back_edge;
			unsigned m_num_bb_float_instrumentation;
			unsigned m_num_bb_float_regs_saved;
			unsigned m_num_style_collafl;

		private:
			string           m_fork_server_entry;  // string to specify fork server entry point
			set<string>      m_exitpoints;         // set of strings to specify exit points
			set<string>      m_whitelist;          // whitelisted functions and/or instructions
			set<string>      m_blacklist;          // blacklisted functions and/or instructions
			zafl_labelid_t   m_labelid;            // internal bookkeeping to generate labels
			zafl_blockid_t   m_blockid;            // internal bookkeeping to generate labels

	};
} 

#endif
