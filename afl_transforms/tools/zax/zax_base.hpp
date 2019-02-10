#ifndef _LIBTRANSFORM_ZAXBASE_H
#define _LIBTRANSFORM_ZAXBASE_H

#include <irdb-core>
#include <irdb-cfg>
#include <irdb-transform>
#include <irdb-util>
#include <irdb-deep>

// utility functions
// @todo: move these functions into other libs for reuse
extern void create_got_reloc(IRDB_SDK::FileIR_t* fir, std::pair<IRDB_SDK::DataScoop_t*,int> wrt, IRDB_SDK::Instruction_t* i);

namespace Zafl
{
	using namespace IRDB_SDK;
	using namespace std;

	using ZaflBlockId_t = uint32_t;
	using ZaflLabelId_t = uint32_t;
	using BBRecord_t = vector<Instruction_t*>;
	using RegisterSet_t = IRDB_SDK::RegisterIDSet_t;

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
			void setDomgraphOptimization(bool);
			void setBasicBlockFloatingInstrumentation(bool);
			void setEnableForkServer(bool);
			void setBreakupCriticalEdges(bool);
			void filterBlocksByDomgraph(BasicBlockSet_t& in_out, const DominatorGraph_t  * dg );
			void filterEntryBlock(BasicBlockSet_t& in_out, BasicBlock_t* p_entry);
			void filterExitBlocks(BasicBlockSet_t& in_out);

		protected:
			ZaxBase_t(pqxxDB_t &p_dbinterface, FileIR_t *p_variantIR, string p_entry, set<string> p_exits, bool p_use_stars=false, bool p_autozafl=false);

			virtual void instrumentBasicBlock(BasicBlock_t *p_bb, const bool p_hasLeafAnnotation, const bool p_collafl_optimization=false) = 0;

			virtual ZaflBlockId_t getBlockId(const unsigned p_maxid=0xFFFF);
			virtual ZaflLabelId_t getLabelId(const unsigned p_maxid=0xFFFF);
			virtual BasicBlockSet_t getBlocksToInstrument (const ControlFlowGraph_t& cfg);
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
			bool hasLeafAnnotation(Function_t* fn) const;
			RegisterSet_t getDeadRegs(Instruction_t* insn) const;
			RegisterSet_t getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const;


		protected:
			pqxxDB_t&                      m_dbinterface;
			unique_ptr<FunctionSet_t>      leaf_functions;
			unique_ptr<DeadRegisterMap_t>  dead_registers;

			bool                           m_use_stars;          // use STARS to have access to dead register info
			bool                           m_autozafl;           // link in library w/ auto fork server
			bool                           m_graph_optimize;     // skip basic blocks based on graph
			bool                           m_domgraph_optimize;  // skip basic blocks based on dominator graph
			bool                           m_forkserver_enabled; // fork server enabled?
			bool                           m_breakupCriticalEdges;
			bool                           m_bb_float_instrumentation;  // skip basic blocks based on graph
			bool                           m_verbose;

			pair<DataScoop_t*,int>         m_trace_map;  // afl shared memory trace map
			pair<DataScoop_t*,int>         m_prev_id;    // id of previous block
			Instruction_t*                 m_plt_zafl_initAflForkServer; // plt entry for afl fork server initialization routine

			map<ZaflBlockId_t, BBRecord_t> m_modifiedBlocks;  // keep track of modified blocks

			// stats
			size_t m_num_bb;
			size_t m_num_bb_instrumented;
			size_t m_num_bb_skipped;
			size_t m_num_bb_skipped_pushjmp;
			size_t m_num_bb_skipped_nop_padding;
			size_t m_num_bb_skipped_innernode;
			size_t m_num_bb_skipped_cbranch;
			size_t m_num_bb_skipped_onlychild;
			size_t m_num_bb_keep_exit_block;
			size_t m_num_bb_keep_cbranch_back_edge;
			size_t m_num_bb_float_instrumentation;
			size_t m_num_bb_float_regs_saved;
			size_t m_num_style_collafl;
			size_t m_num_domgraph_blocks_elided;
			size_t m_num_entry_blocks_elided;
			size_t m_num_exit_blocks_elided;

		private:
			string          m_fork_server_entry;  // string to specify fork server entry point
			set<string>     m_exitpoints;         // set of strings to specify exit points
			set<string>     m_whitelist;          // whitelisted functions and/or instructions
			set<string>     m_blacklist;          // blacklisted functions and/or instructions
			ZaflLabelId_t   m_labelid;            // internal bookkeeping to generate labels
			ZaflBlockId_t   m_blockid;            // internal bookkeeping to generate labels

	};
} 

#endif
