/***************************************************************************
 * Copyright (c)  2018-2019  Zephyr Software LLC. All rights reserved.
 *
 * This software is furnished under a license and/or other restrictive
 * terms and may be used and copied only in accordance with such terms
 * and the inclusion of the above copyright notice. This software or
 * any other copies thereof may not be provided or otherwise made
 * available to any other person without the express written consent
 * of an authorized representative of Zephyr Software LCC. Title to,
 * ownership of, and all rights in the software is retained by
 * Zephyr Software LCC.
 *
 * Zephyr Software LLC. Proprietary Information
 *
 * Unless otherwise specified, the information contained in this
 * directory, following this legend, and/or referenced herein is
 * Zephyr Software LLC. (Zephyr) Proprietary Information.
 *
 * CONTACT INFO
 *
 * E-mail: jwd@zephyr-software.com
 **************************************************************************/

#include <stdlib.h>
#include <string.h> 
#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <irdb-cfg>
#include <irdb-transform>
#include <irdb-elfdep>
#include <irdb-deep>

#include "zax_base.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

#define ALLOF(a) begin(a),end(a)

void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
	(void)fir->addNewRelocation(i,wrt.second, "pcrel", wrt.first);
}

RegisterSet_t ZaxBase_t::getDeadRegs(Instruction_t* insn) const
{
	auto it = dead_registers -> find(insn);
	if(it != dead_registers->end())
		return it->second;
        return RegisterSet_t();
#if 0
        std::pair<MEDS_Annotations_t::iterator,MEDS_Annotations_t::iterator> ret;

        /* find it in the annotations */
        ret = meds_ap_param.getAnnotations().equal_range(insn->getBaseID());
        MEDS_DeadRegAnnotation* p_annotation;

        /* for each annotation for this instruction */
        for (MEDS_Annotations_t::iterator it = ret.first; it != ret.second; ++it)
        {
                        p_annotation=dynamic_cast<MEDS_DeadRegAnnotation*>(it->second);
                        if(p_annotation==NULL)
                                continue;

                        /* bad annotation? */
                        if(!p_annotation->isValid())
                                continue;

                        return p_annotation->getRegisterSet();
        }

        /* couldn't find the annotation, return an empty set.*/
        return RegisterSet_t();
#endif
}

// return intersection of candidates and allowed general-purpose registers
RegisterSet_t ZaxBase_t::getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const
{
	RegisterIDSet_t free_regs;
	set_intersection(candidates.begin(),candidates.end(),allowed.begin(),allowed.end(),
                  std::inserter(free_regs,free_regs.begin()));
	return free_regs;
}

bool ZaxBase_t::hasLeafAnnotation(Function_t* fn) const
{
	auto it = leaf_functions -> find(fn);
	return (it != leaf_functions->end());
#if 0
	assert(fn);
        const auto ret = meds_ap_param.getFuncAnnotations().equal_range(fn->getName());
	const auto sfa_it = find_if(ret.first, ret.second, [](const MEDS_Annotations_FuncPair_t &it)
		{
			auto p_annotation=dynamic_cast<MEDS_SafeFuncAnnotation*>(it.second);
			if(p_annotation==NULL)
				return false;
			return p_annotation->isLeaf();
		}
	);

	return (sfa_it != ret.second);
#endif
}

bool ZaxBase_t::BB_isPaddingNop(const BasicBlock_t *p_bb) const
{
	return p_bb->getInstructions().size()==1 && 
	       p_bb->getPredecessors().size()==0 &&
	       p_bb->getSuccessors().size()==1 &&
	       p_bb->getInstructions()[0]->getDisassembly().find("nop")!=string::npos;
}

bool ZaxBase_t::BB_isPushJmp(const BasicBlock_t *p_bb) const
{
	return p_bb->getInstructions().size()==2 && 
	       p_bb->getInstructions()[0]->getDisassembly().find("push")!=string::npos &&
	       p_bb->getInstructions()[1]->getDisassembly().find("jmp")!=string::npos;
}

ZaxBase_t::ZaxBase_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl)
	:
	Transform(p_variantIR),
	m_dbinterface(p_dbinterface),
	m_use_stars(p_use_stars),
	m_autozafl(p_autozafl),
	m_bb_graph_optimize(false),
	m_domgraph_optimize(false),
	m_forkserver_enabled(true),
	m_breakupCriticalEdges(false),
	m_fork_server_entry(p_forkServerEntryPoint),
	m_exitpoints(p_exitPoints)
{
	if (m_use_stars) {
		cout << "Use STARS analysis engine" << endl;
		// m_stars_analysis_engine.do_STARS(getFileIR());
		auto deep_analysis=DeepAnalysis_t::factory(getFileIR());
		leaf_functions = deep_analysis -> getLeafFunctions();
		dead_registers = deep_analysis -> getDeadRegisters();
	}

	auto ed=ElfDependencies_t::factory(getFileIR());
	if (p_autozafl)
	{
		cout << "autozafl library is on" << endl;
		(void)ed->prependLibraryDepedencies("libautozafl.so");
	}
	else
	{
		cout << "autozafl library is off" << endl;
		(void)ed->prependLibraryDepedencies("libzafl.so");
	}

	// bind to external symbols declared in libzafl.so
	m_plt_zafl_initAflForkServer=ed->appendPltEntry("zafl_initAflForkServer");
	m_trace_map = ed->appendGotEntry("zafl_trace_map");
	m_prev_id = ed->appendGotEntry("zafl_prev_id");

	// let's not instrument these functions ever
	// see isBlacklisted() for other blacklisted functions
	m_blacklist.insert("init");
	m_blacklist.insert("_init");
	m_blacklist.insert("start");
	m_blacklist.insert("_start");
	m_blacklist.insert("fini");
	m_blacklist.insert("_fini");
	m_blacklist.insert("register_tm_clones");
	m_blacklist.insert("deregister_tm_clones");
	m_blacklist.insert("frame_dummy");
	m_blacklist.insert("__do_global_ctors_aux");
	m_blacklist.insert("__do_global_dtors_aux");
	m_blacklist.insert("__libc_csu_init");
	m_blacklist.insert("__libc_csu_fini");
	m_blacklist.insert("__libc_start_main");
	m_blacklist.insert("__gmon_start__");
	m_blacklist.insert("__cxa_atexit");
	m_blacklist.insert("__cxa_finalize");
	m_blacklist.insert("__assert_fail");
	m_blacklist.insert("free");
	m_blacklist.insert("fnmatch");
	m_blacklist.insert("readlinkat");
	m_blacklist.insert("malloc");
	m_blacklist.insert("calloc");
	m_blacklist.insert("realloc");
	m_blacklist.insert("argp_failure");
	m_blacklist.insert("argp_help");
	m_blacklist.insert("argp_state_help");
	m_blacklist.insert("argp_error");
	m_blacklist.insert("argp_parse");

	m_verbose = false;
	m_bb_float_instrumentation = false;

	m_labelid = 0;
	m_blockid = 0;

	// stats
	m_num_bb = 0;
	m_num_bb_instrumented = 0;
	m_num_bb_skipped = 0;
	m_num_bb_skipped_pushjmp = 0;
	m_num_bb_skipped_nop_padding = 0;
	m_num_bb_skipped_innernode = 0;
	m_num_bb_skipped_cbranch = 0;
	m_num_bb_skipped_onlychild = 0;
	m_num_bb_keep_exit_block = 0;
	m_num_bb_keep_cbranch_back_edge = 0;
	m_num_style_collafl = 0;
	m_num_bb_float_instrumentation = 0;
	m_num_bb_float_regs_saved = 0;
	m_num_domgraph_blocks_elided = 0;
}

void ZaxBase_t::setVerbose(bool p_verbose)
{
	m_verbose = p_verbose;
}

void ZaxBase_t::setBasicBlockOptimization(bool p_bb_graph_optimize) 
{
	m_bb_graph_optimize = p_bb_graph_optimize;
	const auto enabled = m_bb_graph_optimize ? "enable" : "disable";
	cout << enabled << " basic block optimization" << endl ;
}

void ZaxBase_t::setDomgraphOptimization(bool p_domgraph_optimize) 
{
	m_domgraph_optimize = p_domgraph_optimize;
	const auto enabled = m_domgraph_optimize ? "enable" : "disable";
	cout << enabled << " dominator graph optimization" << endl ;
}


void ZaxBase_t::setEnableForkServer(bool p_forkserver_enabled) 
{
	m_forkserver_enabled = p_forkserver_enabled;
}

void ZaxBase_t::setBreakupCriticalEdges(bool p_breakupEdges)
{
	m_breakupCriticalEdges = p_breakupEdges;
	m_breakupCriticalEdges ?
		cout << "enable breaking of critical edges" << endl :
		cout << "disable breaking of critical edges" << endl;
}

void ZaxBase_t::setBasicBlockFloatingInstrumentation(bool p_float)
{
	m_bb_float_instrumentation = p_float;
	m_bb_float_instrumentation ?
		cout << "enable floating instrumentation" << endl :
		cout << "disable floating instrumentation" << endl;
}

bool ZaxBase_t::getBasicBlockFloatingInstrumentation() const
{
	return m_bb_float_instrumentation;
}

/*
 * Only allow instrumentation in whitelisted functions/instructions
 * Each line in file is either a function name or address
 */
void ZaxBase_t::setWhitelist(const string& p_whitelist)
{
	std::ifstream whitelistFile(p_whitelist);
	if (!whitelistFile.is_open())
		throw;
	std::string line;
	while(whitelistFile >> line)
	{
		cout <<"Adding " << line << " to white list" << endl;
		m_whitelist.insert(line);
	}
	whitelistFile.close();
}

/*
 * Disallow instrumentation in blacklisted functions/instructions
 * Each line in file is either a function name or address
 */
void ZaxBase_t::setBlacklist(const string& p_blackList)
{
	std::ifstream blackListFile(p_blackList);
	if (!blackListFile.is_open())
		throw;
	std::string line;
	while(blackListFile >> line)
	{
		cout <<"Adding " << line << " to black list" << endl;
		m_blacklist.insert(line);
	}
	blackListFile.close();
}

ZaflLabelId_t ZaxBase_t::getLabelId(const unsigned p_max) 
{
	return m_labelid++;
}

ZaflBlockId_t ZaxBase_t::getBlockId(const unsigned p_max)
{
	m_blockid = (m_blockid+1) % p_max;
	return m_blockid;
}

void ZaxBase_t::insertExitPoint(Instruction_t *p_inst)
{
	assert(p_inst->getAddress()->getVirtualOffset());

	if (p_inst->getFunction())
		cout << "in function: " << p_inst->getFunction()->getName() << " ";

	stringstream ss;
	ss << hex << p_inst->getAddress()->getVirtualOffset();
	m_blacklist.insert(ss.str());

	cout << "insert exit point at: 0x" << ss.str() << endl;
	
	auto tmp = p_inst;
	     insertAssemblyBefore(tmp, "xor edi, edi"); //  rdi=0
	tmp = insertAssemblyAfter(tmp, "mov eax, 231"); //  231 = __NR_exit_group   from <asm/unistd_64.h>
	tmp = insertAssemblyAfter(tmp, "syscall");      //  sys_exit_group(edi)
}

void ZaxBase_t::insertForkServer(Instruction_t* p_entry)
{
	assert(p_entry);

	stringstream ss;
	ss << "0x" << hex << p_entry->getAddress()->getVirtualOffset();
	cout << "inserting fork server code at address: " << ss.str() << dec << endl;
	assert(p_entry->getAddress()->getVirtualOffset());

	if (p_entry->getFunction()) {
		cout << " function: " << p_entry->getFunction()->getName();
		cout << " ep instr: " << p_entry->getDisassembly() << endl;
	}
	cout << endl;

	// blacklist insertion point
	cout << "Blacklisting entry point: " << ss.str() << endl;
	m_blacklist.insert(ss.str());

	// insert the instrumentation
	auto tmp=p_entry;
	const auto regs = vector<string>({ "rdi", "rsi", "rbp", "rdx", "rcx", "rbx", "rax", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"});

	// red zone
	(void)insertAssemblyBefore(tmp, "lea rsp, [rsp-128]");
	// save flags and registrers
	tmp = insertAssemblyAfter(tmp,  "pushf ") ;
	for (vector<string>::const_iterator rit = regs.begin(); rit != regs.end(); ++rit)
		tmp = insertAssemblyAfter(tmp, " push " + *rit);
	// call fork server initialization routine (in external library)
	tmp = insertAssemblyAfter(tmp,  "call 0 ", m_plt_zafl_initAflForkServer) ;
	// restore registers and flags
	for (vector<string>::const_reverse_iterator rit = regs.rbegin(); rit != regs.rend(); ++rit)
    		tmp = insertAssemblyAfter(tmp, " pop " + *rit) ;
	tmp = insertAssemblyAfter(tmp,  "popf ") ;
	// red zome
	tmp = insertAssemblyAfter(tmp,  "lea rsp, [rsp+128]");
}

void ZaxBase_t::insertForkServer(string p_forkServerEntry)
{
	assert(p_forkServerEntry.size() > 0);

	cout << "looking for fork server entry point: " << p_forkServerEntry << endl;

	if (std::isdigit(p_forkServerEntry[0]))
	{
		// find instruction to insert fork server based on address
		const auto voffset = (VirtualOffset_t) std::strtoul(p_forkServerEntry.c_str(), NULL, 16);
		auto instructions=find_if(getFileIR()->getInstructions().begin(), getFileIR()->getInstructions().end(), [&](const Instruction_t* i) {
				return i->getAddress()->getVirtualOffset()==voffset;
			});

		if (instructions==getFileIR()->getInstructions().end())
		{
			cerr << "Error: could not find address to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		insertForkServer(*instructions);
	}
	else
	{
		// find entry point of specified function to insert fork server
		auto entryfunc=find_if(getFileIR()->getFunctions().begin(), getFileIR()->getFunctions().end(), [&](const Function_t* f) {
				return f->getName()==p_forkServerEntry;
			});

		
		if(entryfunc==getFileIR()->getFunctions().end())
		{
			cerr << "Error: could not find function to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		cout << "inserting fork server code at entry point of function: " << p_forkServerEntry << endl;
		auto entrypoint = (*entryfunc)->getEntryPoint();
		
		if (!entrypoint) 
		{
			cerr << "Could not find entry point for: " << p_forkServerEntry << endl;
			throw;
		}
		insertForkServer(entrypoint);
	}
}

void ZaxBase_t::setupForkServer()
{
	if (m_fork_server_entry.size()>0)
	{
		// user has specified entry point
		insertForkServer(m_fork_server_entry);
	}
	else
	{
		// try to insert fork server at main
		const auto &all_funcs=getFileIR()->getFunctions();
		const auto main_func_it=find_if(all_funcs.begin(), all_funcs.end(), [&](const Function_t* f) { return f->getName()=="main";});
		if(main_func_it!=all_funcs.end())
		{
			insertForkServer("main"); 
		}

	}

	// it's ok not to have a fork server at all, e.g. libraries
}

void ZaxBase_t::insertExitPoints()
{
	for (auto exitp : m_exitpoints)
	{
		if (std::isdigit(exitp[0]))
		{
			// find instruction to insert fork server based on address
			const auto voffset = (VirtualOffset_t) std::strtoul(exitp.c_str(), NULL, 16);
			auto instructions=find_if(getFileIR()->getInstructions().begin(), getFileIR()->getInstructions().end(), [&](const Instruction_t* i) {
					return i->getAddress()->getVirtualOffset()==voffset;
				});

			if (instructions==getFileIR()->getInstructions().end())
			{
				cerr << "Error: could not find address to insert exit point: " << exitp << endl;
				throw;
			}

			insertExitPoint(*instructions);
		}
		else
		{
			// find function by name
			auto func_iter=find_if(getFileIR()->getFunctions().begin(), getFileIR()->getFunctions().end(), [&](const Function_t* f) {
				return f->getName()==exitp;
			});

		
			if(func_iter==getFileIR()->getFunctions().end())
			{
				cerr << "Error: could not find function to insert exit points: " << exitp << endl;
				throw;
			}

			cout << "inserting exit code at return points of function: " << exitp << endl;
			for (auto i : (*func_iter)->getInstructions())
			{
				if (i->getBaseID() >= 0)
				{
					const auto d=DecodedInstruction_t::factory(i);

					// if it's a return instruction, add exit point
					if (d->isReturn())
					{
						insertExitPoint(i);
					}
				}
			}
		}
	}
}

// blacklist functions:
//     - in blacklist
//     - that start with '.'
//     - that end with @plt
bool ZaxBase_t::isBlacklisted(const Function_t *p_func) const
{
	return (p_func->getName()[0] == '.' || 
	        p_func->getName().find("@plt") != string::npos ||
	        m_blacklist.find(p_func->getName())!=m_blacklist.end());
}

bool ZaxBase_t::isWhitelisted(const Function_t *p_func) const
{
	if (m_whitelist.size() == 0) return true;
	return (m_whitelist.find(p_func->getName())!=m_whitelist.end());
}

bool ZaxBase_t::isBlacklisted(const Instruction_t *p_inst) const
{
	stringstream ss;
	ss << "0x" << hex << p_inst->getAddress()->getVirtualOffset();
	return (m_blacklist.count(ss.str()) > 0 || isBlacklisted(p_inst->getFunction()));
}

bool ZaxBase_t::isWhitelisted(const Instruction_t *p_inst) const
{
	if (m_whitelist.size() == 0) return true;

	stringstream ss;
	ss << "0x" << hex << p_inst->getAddress()->getVirtualOffset();
	return (m_whitelist.count(ss.str()) > 0 || isWhitelisted(p_inst->getFunction()));
}

void ZaxBase_t::setup()
{
	if (m_forkserver_enabled)
		setupForkServer();
	else
		cout << "Fork server has been disabled" << endl;

	insertExitPoints();
}

void ZaxBase_t::teardown()
{
	dumpAttributes();
	dumpMap();
}	

// in: control flow graph for a given function
// out: set of basic blocks to instrument
BasicBlockSet_t ZaxBase_t::getBlocksToInstrument(const ControlFlowGraph_t &cfg)
{
	static int bb_debug_id=-1;

	if (m_verbose)
		cout << cfg << endl;

	auto keepers = BasicBlockSet_t();

	for (auto &bb : cfg.getBlocks())
	{
		assert(bb->getInstructions().size() > 0);

		bb_debug_id++;

		// already marked as a keeper
		if (keepers.find(bb) != keepers.end())
			continue;
 
		// if whitelist specified, only allow instrumentation for functions/addresses in whitelist
		if (!isWhitelisted(bb->getInstructions()[0]))
			continue;

		if (isBlacklisted(bb->getInstructions()[0]))
			continue;

		// debugging support
		if (getenv("ZAFL_LIMIT_BEGIN"))
		{
			if (bb_debug_id < atoi(getenv("ZAFL_LIMIT_BEGIN")))
				continue;	
		}

		// debugging support
		if (getenv("ZAFL_LIMIT_END"))
		{
			if (bb_debug_id >= atoi(getenv("ZAFL_LIMIT_END"))) 
				continue;
		}

		// make sure we're not trying to instrument code we just inserted, e.g., fork server, added exit points
		if (bb->getInstructions()[0]->getBaseID() < 0)
			continue;

		// push/jmp pair, don't bother instrumenting
		if (BB_isPushJmp(bb))
		{
			m_num_bb_skipped_pushjmp++;
			continue;
		}

		// padding nop, don't bother
		if (BB_isPaddingNop(bb))
		{
			m_num_bb_skipped_nop_padding++;
			continue;
		}

		if (m_bb_graph_optimize)
		{
			const auto has_unique_preds=
				[&](const BasicBlockSet_t& bbs) -> bool
				{
					for (const auto & b : bbs)
					{
						if (b->getPredecessors().size() != 1)
							return false;
					}
					return true;
				};
			const auto has_ibta=
				[&](const BasicBlockSet_t& successors) -> bool
				{
					for (const auto & s : successors)
					{
						if (s->getInstructions()[0]->getIndirectBranchTargetAddress())
							return true;
					}
					return false;
				};

			if (bb->getSuccessors().size() == 2 && 
			    bb->endsInConditionalBranch() && 
				has_unique_preds(bb->getSuccessors()) &&
			    !has_ibta(bb->getSuccessors()))
			{
				// for now, until we get a more principled way of pruning the graph,
				// make sure to keep both successors
				for (auto next_bb : bb->getSuccessors())
					keepers.insert(next_bb);

				m_num_bb_skipped_cbranch++;
				continue;
			}
		}

		keepers.insert(bb);
	}
	return keepers;
}

void ZaxBase_t::filterBlocksByDomgraph(BasicBlockSet_t& in_out,  const DominatorGraph_t* dg)
{
	if(!m_domgraph_optimize)
		return;
	auto copy=in_out;
	for(auto block : copy)
	{
		auto &successors = block->getSuccessors();

		const auto is_leaf_block = successors.size() == 0;

		const auto is_non_dominated=
			[&](const BasicBlock_t* successor) -> bool
			{
				const auto &dominators = dg->getDominators(successor);
				return dominators.find(block) != end(dominators);
			};
		auto non_dominator_successor_it = find_if(ALLOF(successors), is_non_dominated);
		const auto has_non_dominator_successor = non_dominator_successor_it != end(successors);
		const auto keep = (is_leaf_block || has_non_dominator_successor);
		if(!keep)
		{
			in_out.erase(block);
			m_num_domgraph_blocks_elided++;
		}
	}
}

// by default, return the first instruction in block
Instruction_t* ZaxBase_t::getInstructionToInstrument(const BasicBlock_t *p_bb, const unsigned p_num_free_regs_desired)
{
	if (!p_bb) 
		return nullptr;

	const auto first_instruction = p_bb->getInstructions()[0];

	// no STARS (i.e., no dead reg annotations)
	// or floating instrumentation turned off
	if (!getBasicBlockFloatingInstrumentation() || !m_use_stars)
	{
		for (auto i : p_bb->getInstructions())
		{
			if (i->getBaseID())
				return i;
		}

		// fallback: return the first instruction
		return first_instruction;
	}

	// scan basic block looking for instruction with requested number of free regs
	const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
	// auto &ap = m_stars_analysis_engine.getAnnotations();
	auto best_i = first_instruction;
	auto max_free_regs = 0U;
	auto num_free_regs_best_i = 0U;
	auto num_free_regs_first_instruction = 0U;

	for (auto i : p_bb->getInstructions())
	{
		const auto dead_regs = getDeadRegs(i);
		const auto num_free_regs = getFreeRegs(dead_regs, allowed_regs).size();

		if (i == first_instruction)
			num_free_regs_first_instruction = num_free_regs;

		if (num_free_regs >= p_num_free_regs_desired)
		{
			// found instruction with requested number of free registers
			m_num_bb_float_instrumentation++;
			if (i != first_instruction)
			{
				const auto num_saved = std::max(static_cast<long unsigned>(p_num_free_regs_desired), num_free_regs) - num_free_regs_first_instruction;
				m_num_bb_float_regs_saved += num_saved;
			}
			return i;
		}

		// keep track of the best thus far
		if (num_free_regs > max_free_regs)
		{
			max_free_regs = num_free_regs;
			best_i = i;
			num_free_regs_best_i = num_free_regs;
		}
	}

	if (best_i != first_instruction) 
	{
		const auto num_saved = std::max(p_num_free_regs_desired, num_free_regs_best_i) - num_free_regs_first_instruction;
		m_num_bb_float_regs_saved += num_saved;
		m_num_bb_float_instrumentation++;
	}

	return best_i;
}

/*
 * Execute the transform.
 *
 * preconditions: the FileIR is read as from the IRDB. valid file listing functions to auto-initialize
 * postcondition: instructions added to auto-initialize stack for each specified function
 *
 */
int ZaxBase_t::execute()
{
	setup();

	// for all functions
	//    build cfg and extract basic blocks
	//    for all basic blocks, figure out whether should be kept
	//    for all kept basic blocks
	//          add afl-compatible instrumentation
	
	struct BaseIDSorter
	{
		bool operator()( const Function_t* lhs, const Function_t* rhs ) const 
		{
			assert(lhs->getBaseID() != BaseObj_t::NOT_IN_DATABASE);
			assert(rhs->getBaseID() != BaseObj_t::NOT_IN_DATABASE);
			return lhs->getBaseID() < rhs->getBaseID();
		}
	};
	auto sortedFuncs=set<Function_t*, BaseIDSorter>( ALLOF(getFileIR()->getFunctions()));
	for(auto f :  sortedFuncs)
	{
		if (f == nullptr )       continue;
		// skip instrumentation for blacklisted functions 
		if (isBlacklisted(f))    continue;
		// skip if function has no entry point
		if (!f->getEntryPoint()) continue;

		bool leafAnnotation = true;
		if (m_use_stars) 
		{
			leafAnnotation = hasLeafAnnotation(f);
		}

		const auto cfgp = ControlFlowGraph_t::factory(f);
		const auto &cfg = *cfgp;
		const auto dom_graphp=DominatorGraph_t::factory(cfgp.get());
		const auto has_domgraph_warnings = dom_graphp -> hasWarnings();  

		const auto num_blocks_in_func = cfg.getBlocks().size();
		m_num_bb += num_blocks_in_func;
		
		auto keepers = getBlocksToInstrument(cfg);
		if(!has_domgraph_warnings)
			filterBlocksByDomgraph(keepers,dom_graphp.get());

		struct BBSorter
		{
			bool operator()( const BasicBlock_t* lhs, const BasicBlock_t* rhs ) const 
			{
				const auto lhs_insns=lhs->getInstructions();
				const auto rhs_insns=rhs->getInstructions();
				assert(lhs_insns[0]->getBaseID() != BaseObj_t::NOT_IN_DATABASE);	
				assert(rhs_insns[0]->getBaseID() != BaseObj_t::NOT_IN_DATABASE);	
				return lhs_insns[0]->getBaseID() < rhs_insns[0]->getBaseID();
			}
		};
		auto sortedBasicBlocks = set<BasicBlock_t*, BBSorter> (ALLOF(keepers));
		for (auto &bb : sortedBasicBlocks)
		{
			auto collAflSingleton = false;
			// for collAfl-style instrumentation, we want #predecessors==1
			// if the basic block entry point is an IBTA, we don't know the #predecessors
			if (m_bb_graph_optimize               && 
			    bb->getPredecessors().size() == 1 && 
			    !bb->getInstructions()[0]->getIndirectBranchTargetAddress()
			   )
			{
				collAflSingleton = true;
				m_num_style_collafl++;

			}

			instrumentBasicBlock(bb, leafAnnotation, collAflSingleton);
		}

		m_num_bb_instrumented += keepers.size();
		m_num_bb_skipped += (num_blocks_in_func - keepers.size());

		if (m_verbose)
		{
			cout << "Post transformation CFG:" << endl;
			auto post_cfg=ControlFlowGraph_t::factory(f);	
			cout << *post_cfg << endl;
		}

		cout << "Function " << f->getName() << ":  " << dec << keepers.size() << "/" << num_blocks_in_func << " basic blocks instrumented." << endl;
	};

	teardown();

	return 1;
}

void ZaxBase_t::dumpAttributes()
{
	cout << "#ATTRIBUTE num_bb=" << dec << m_num_bb << endl;
	cout << "#ATTRIBUTE num_bb_instrumented=" << m_num_bb_instrumented << endl;
	cout << "#ATTRIBUTE num_bb_skipped=" << m_num_bb_skipped << endl;
	cout << "#ATTRIBUTE num_bb_skipped_pushjmp=" << m_num_bb_skipped_pushjmp << endl;
	cout << "#ATTRIBUTE num_bb_skipped_nop_padding=" << m_num_bb_skipped_nop_padding << endl;
	cout << "#ATTRIBUTE num_bb_float_instrumentation=" << m_num_bb_float_instrumentation << endl;
	cout << "#ATTRIBUTE num_bb_float_register_saved=" << m_num_bb_float_regs_saved << endl;
	cout << "#ATTRIBUTE graph_optimize=" << boolalpha << m_bb_graph_optimize << endl;
	if (m_bb_graph_optimize)
	{
		cout << "#ATTRIBUTE num_bb_skipped_cond_branch=" << m_num_bb_skipped_cbranch << endl;
		cout << "#ATTRIBUTE num_bb_keep_cbranch_back_edge=" << m_num_bb_keep_cbranch_back_edge << endl;
		cout << "#ATTRIBUTE num_bb_keep_exit_block=" << m_num_bb_keep_exit_block << endl;
		cout << "#ATTRIBUTE num_style_collafl=" << m_num_style_collafl << endl;
		cout << "#ATTRIBUTE num_bb_skipped_onlychild=" << m_num_bb_skipped_onlychild << endl;
		cout << "#ATTRIBUTE num_bb_skipped_innernode=" << m_num_bb_skipped_innernode << endl;
	}
	cout << "#ATTRIBUTE num_domgraph_blocks_elided=" << m_num_domgraph_blocks_elided++ << endl;
}

// file dump of modified basic block info
void ZaxBase_t::dumpMap()
{
	getFileIR()->setBaseIDS();           // make sure instructions have IDs
	getFileIR()->assembleRegistry();     // make sure to assemble all instructions

	std::ofstream mapfile("zax.map");

	mapfile << "# BLOCK_ID  ID_EP:size  ID_OLDEP:size (ID_INSTRUMENTATION:size)*" << endl;
	for (auto &mb : m_modifiedBlocks)
	{
		const auto blockid = mb.first;
		mapfile << dec << blockid << " ";
		for (auto &entry : mb.second)
		{
			mapfile << hex << entry->getBaseID() << ":" << dec << entry->getDataBits().size() << " ";
		}
		mapfile << endl;
	}
}
