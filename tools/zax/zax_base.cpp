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
#include "critical_edge_breaker.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

#define ALLOF(a) begin(a),end(a)
#define FIRSTOF(a) (*(begin(a)))

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
}

// return intersection of candidates and allowed general-purpose registers
RegisterSet_t ZaxBase_t::getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const
{
	RegisterIDSet_t free_regs;
	set_intersection(ALLOF(candidates), ALLOF(allowed), std::inserter(free_regs,free_regs.begin()));
	return free_regs;
}

bool ZaxBase_t::hasLeafAnnotation(Function_t* fn) const
{
	auto it = leaf_functions -> find(fn);
	return (it != leaf_functions->end());
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
	Transform_t(p_variantIR),
	m_dbinterface(p_dbinterface),
	m_use_stars(p_use_stars),
	m_autozafl(p_autozafl),
	m_graph_optimize(false),
	m_domgraph_optimize(false),
	m_forkserver_enabled(true),
	m_breakupCriticalEdges(false),
	m_fork_server_entry(p_forkServerEntryPoint),
	m_exitpoints(p_exitPoints),
	m_do_fixed_addr_optimization(false),
	m_trace_map_fixed_addr(0),
	m_previd_fixed_addr(0),
	m_context_fixed_addr(0)
{
	if (m_use_stars) {
		cout << "Use STARS analysis engine" << endl;
		auto deep_analysis=DeepAnalysis_t::factory(getFileIR());
		leaf_functions = deep_analysis -> getLeafFunctions();
		dead_registers = deep_analysis -> getDeadRegisters();
	}

	auto ed=ElfDependencies_t::factory(getFileIR());
	if (p_autozafl)
	{
		cout << "autozafl library is on" << endl;
		(void)ed->appendLibraryDepedencies("libautozafl.so");
	}
	else
	{
		cout << "autozafl library is off" << endl;
		(void)ed->appendLibraryDepedencies("libzafl.so");
	}

	// bind to external symbols declared in libzafl.so
	m_plt_zafl_initAflForkServer=ed->appendPltEntry("zafl_initAflForkServer");
	m_trace_map = ed->appendGotEntry("zafl_trace_map");
	m_prev_id = ed->appendGotEntry("zafl_prev_id");
	m_context_id = ed->appendGotEntry("zafl_context");

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

	setContextSensitivity(ContextSensitivity_None);

	m_entry_point = nullptr;

	m_labelid = 0;
	m_blockid = 0;

	// stats
	m_num_bb = 0;
	m_num_bb_instrumented = 0;
	m_num_bb_skipped = 0;
	m_num_bb_skipped_pushjmp = 0;
	m_num_bb_skipped_nop_padding = 0;
	m_num_bb_skipped_cbranch = 0;
	m_num_style_collafl = 0;
	m_num_bb_float_instrumentation = 0;
	m_num_bb_float_regs_saved = 0;
	m_num_domgraph_blocks_elided = 0;
	m_num_exit_blocks_elided = 0;
	m_num_entry_blocks_elided = 0;
	m_num_single_block_function_elided = 0;
	m_num_contexts = 0;
	m_num_contexts_entry = 0;
	m_num_contexts_exit = 0;

}

bool ZaxBase_t::useFixedAddresses() const
{
	return m_do_fixed_addr_optimization;
}

unsigned long ZaxBase_t::getFixedAddressMap() const
{
	return m_trace_map_fixed_addr;
}

unsigned long ZaxBase_t::getFixedAddressPrevId() const
{
	return m_previd_fixed_addr;
}

unsigned long ZaxBase_t::getFixedAddressContext() const
{
	return m_context_fixed_addr;
}

void ZaxBase_t::setVerbose(bool p_verbose)
{
	m_verbose = p_verbose;
}

void ZaxBase_t::setFixedMapAddress(VirtualOffset_t p_fixed_addr)
{
	cout << "Setting fixed-map address to "<< hex << p_fixed_addr << endl;
        m_trace_map_fixed_addr       = p_fixed_addr;
        m_do_fixed_addr_optimization = (p_fixed_addr != 0);
	if (m_do_fixed_addr_optimization) 
	{
		cout << "fixed address optimization enabled" << endl;
		// must match values in libzafl.so
		// @todo: include libzafl.hpp
		const auto trace_map_size = 65536; // power of 2 
		const auto gap = 4096;             // page, multiple of 4K
		const auto previd_offset = 32;     // word aligned
		const auto context_offset = 64;    // word aligned (make sure no overlap with previd)
		m_previd_fixed_addr = m_trace_map_fixed_addr + trace_map_size + gap + previd_offset;
		m_context_fixed_addr = m_trace_map_fixed_addr + trace_map_size + gap + context_offset;
		cout << hex;
		cout << "tracemap fixed at: 0x" << m_trace_map_fixed_addr << endl;
		cout << "prev_id fixed at : 0x" << m_previd_fixed_addr << endl;
		cout << "context fixed at : 0x" << m_context_fixed_addr << endl;
		cout << dec;
	}
	else
	{
		cout << "fixed address optimization disabled" << endl;
		m_trace_map_fixed_addr = 0;
		m_previd_fixed_addr = 0;
		m_context_fixed_addr = 0;
	}
}

void ZaxBase_t::setBasicBlockOptimization(bool p_bb_graph_optimize) 
{
	m_graph_optimize = p_bb_graph_optimize;
	const auto enabled = m_graph_optimize ? "enable" : "disable";
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

void ZaxBase_t::setContextSensitivity(ContextSensitivity_t p_context_style)
{
	m_context_sensitivity = p_context_style;
	switch (m_context_sensitivity)
	{
		case ContextSensitivity_None:
			cout << "disable context sensitivity" << endl;
			break;
		case ContextSensitivity_Function:
			cout << "enable context sensitivity (style: function)" << endl;
			break;
		case ContextSensitivity_Callsite:
			cout << "enable context sensitivity (style: callsite)" << endl;
			break;
	}
}

ContextSensitivity_t ZaxBase_t::getContextSensitivity() const
{
	return m_context_sensitivity;
}

/*
 * Only allow instrumentation in whitelisted functions/instructions
 * Each line in file is either a function name or address
 */
void ZaxBase_t::setWhitelist(const string& p_whitelist)
{
	std::ifstream whitelistFile(p_whitelist);
	if (!whitelistFile.is_open())
		throw std::runtime_error("Could not open file " + p_whitelist);
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

ZaflContextId_t ZaxBase_t::getContextId(const unsigned p_max)
{
       auto counter = 0;
       auto contextid = 0;

       // only try getting new context id 100 times
       // avoid returning duplicate if we can help it
       while (counter++ < 100) {
               contextid = rand() % p_max; 
               if (m_used_contextid.find(contextid) == m_used_contextid.end())
               {
                       m_used_contextid.insert(contextid);
                       return contextid;
               }
       }
       return contextid % p_max;
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

	m_entry_point = p_entry;

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

		cout << "requested inserting fork server code at entry point of function: " << p_forkServerEntry << endl;

		const auto entrypoint = (*entryfunc)->getEntryPoint();
		auto insertion_point = entrypoint;
		auto cfg=ControlFlowGraph_t::factory(*entryfunc);	
		for (auto &bb : cfg->getBlocks())
		{
			if (bb->getInstructions()[0] == entrypoint)
			{
				const auto idx = bb->getInstructions().size()-1;
				insertion_point = bb->getInstructions()[idx];
				cout << "override: pre-inserting fork server code at last instruction of entry point block of function: 0x" << hex << insertion_point->getAddress()->getVirtualOffset() << " : " << insertion_point->getDisassembly() << endl;
				break;
			}

		}
		
		if (!entrypoint) 
		{
			cerr << "Could not find entry point for: " << p_forkServerEntry << endl;
			throw;
		}
		insertForkServer(insertion_point);
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

 	getFileIR()->assembleRegistry();
 	getFileIR()->setBaseIDS();

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

 	getFileIR()->assembleRegistry();
 	getFileIR()->setBaseIDS();
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
	return (m_blacklist.count(ss.str()) > 0 || isBlacklisted(p_inst->getFunction()) || p_inst == m_entry_point);
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
		{
			if (m_verbose)
				cout << "Base ID < 0" << endl;
			continue;
		}

		// push/jmp pair, don't bother instrumenting
		if (BB_isPushJmp(bb))
		{
			m_num_bb_skipped_pushjmp++;
			continue;
		}

		keepers.insert(bb);
	}
	return keepers;
}

void ZaxBase_t::filterPaddingNOP(BasicBlockSet_t& p_in_out)
{
	auto copy=p_in_out;
	for(auto block : copy)
	{
		if (BB_isPaddingNop(block))
		{
			p_in_out.erase(block);
			m_num_bb_skipped_nop_padding++;
		}
	}
}

void ZaxBase_t::filterEntryBlock(BasicBlockSet_t& p_in_out, BasicBlock_t* p_entry)
{
	if (p_entry->getSuccessors().size() != 1)
		return;

	if (p_in_out.find(p_entry) == p_in_out.end())
		return;

	if (p_in_out.find(*(p_entry->getSuccessors().begin())) == p_in_out.end())
		return;

	// both entry and successor are in <p_in_out>
	// entry block has single successor
	p_in_out.erase(p_entry);
	m_num_entry_blocks_elided++;
	if (m_verbose) {
		cout << "Eliding entry block" << endl; 
	}
}

void ZaxBase_t::filterExitBlocks(BasicBlockSet_t& p_in_out)
{
	auto copy=p_in_out;
	for(auto block : copy)
	{
		if (!block->getIsExitBlock())
			continue;

		if (block->getInstructions()[0]->getIndirectBranchTargetAddress())
			continue;

		if (block->getPredecessors().size() != 1)
			continue;

		if (copy.find(*block->getPredecessors().begin()) == copy.end())
			continue;

		const auto last_instruction_index = block->getInstructions().size() - 1;
		if (block->getInstructions()[last_instruction_index]->getDisassembly().find("ret")==string::npos)
			continue;

		// must be an exit block (ret)
		// exit block is not an ibta
		// only 1 predecessor
		// predecessor in <p_in_out>
		p_in_out.erase(block);
		m_num_exit_blocks_elided++;
		if (m_verbose) {
			cout << "Eliding exit block" << endl; 
		}
	}
}

void ZaxBase_t::filterConditionalBranches(BasicBlockSet_t& p_in_out)
{
	if (!m_graph_optimize)
		return;
	auto copy=p_in_out;
	for(auto block : copy)
	{
		const auto successors_have_unique_preds = 
			find_if(ALLOF(block->getSuccessors()), [](const BasicBlock_t* s)
				{
					return s->getPredecessors().size() > 1;
				}) == block->getSuccessors().end();

		const auto successor_with_ibta = 
			find_if(ALLOF(block->getSuccessors()), [](const BasicBlock_t* s)
				{
					return s->getInstructions()[0]->getIndirectBranchTargetAddress();
				}) != block->getSuccessors().end();
	
		const auto all_successors_kept = 
			find_if(ALLOF(block->getSuccessors()), [p_in_out](BasicBlock_t* s)
				{
					return p_in_out.find(s) == p_in_out.end();
				}) == block->getSuccessors().end();

		if (block->endsInConditionalBranch() && 
		    all_successors_kept &&
		    successors_have_unique_preds &&
		    !successor_with_ibta)
		{
			// block ends in conditional branch
			// successors are in <p_in_out>
			// successors have unique predecessors
			// no successor is an ibta
			if (m_verbose)
				cout << "Eliding conditional branch -- keeping successors" << endl;
			p_in_out.erase(block);
			m_num_bb_skipped_cbranch++;
			continue;
		}
	}
}

void ZaxBase_t::filterBlocksByDomgraph(BasicBlockSet_t& p_in_out,  const DominatorGraph_t* dg)
{
	if(!m_domgraph_optimize)
		return;

	if(m_verbose)
	{
		cout<<"And the Dominator graph is:" <<endl;
		cout<<*dg<<endl;
	}

	auto copy=p_in_out;
	for(auto block : copy)
	{
		const auto &dominates = dg->getDominated(block);

		const auto is_dg_leaf = dominates.size()==1; // leaf in the dom tree -- we dominate ourselves.
				// this is leaf of cfg: successors.size() == 0;

		const auto is_dominated=
			[&](const BasicBlock_t* successor) -> bool
			{
				const auto &dominators = dg->getDominators(successor);
				return dominators.find(block) != end(dominators);
			};
		const auto is_non_dominated= [&](const BasicBlock_t* successor) -> bool
			{
				return !is_dominated(successor);
			};

		auto &successors = block->getSuccessors();
		auto non_dominator_successor_it = find_if(ALLOF(successors), is_non_dominated);
		const auto has_non_dominator_successor = non_dominator_successor_it != end(successors);
		const auto keep = (is_dg_leaf || has_non_dominator_successor);
		if(!keep)
		{
			p_in_out.erase(block);
			m_num_domgraph_blocks_elided++;
			if(m_verbose)
			{
				cout<<"Eliding instrumentation in block id      = " << dec << block->getInstructions()[0]->getBaseID() << endl;
				cout<<"is_dg_leaf            = " << boolalpha << is_dg_leaf << endl;
				cout<<"has_non_dom_successor = " << boolalpha << has_non_dominator_successor << endl;
			}
		}
		else
		{
			if(m_verbose)
			{
				cout<<"Instrumenting block id      = " << dec << block->getInstructions()[0]->getBaseID() << endl;
				cout<<"is_dg_leaf            = " << boolalpha << is_dg_leaf << endl;
				cout<<"has_non_dom_successor = " << boolalpha << has_non_dominator_successor << endl;
			}
		}
	}
}

// by default, return the first instruction in block
Instruction_t* ZaxBase_t::getInstructionToInstrument(const BasicBlock_t *p_bb, const unsigned p_num_free_regs_desired)
{
	if (!p_bb) 
		return nullptr;

	const auto first_instruction = p_bb->getInstructions()[0];

	if (p_num_free_regs_desired == 0)
		return first_instruction;

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
	const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_RSI, rn_RDI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
	// auto &ap = m_stars_analysis_engine.getAnnotations();
	auto best_i = first_instruction;
	auto max_free_regs = 0U;
	auto num_free_regs_best_i = 0U;
	auto num_free_regs_first_instruction = 0U;

	for (auto i : p_bb->getInstructions())
	{
		if (isBlacklisted(i))
			continue;

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

void ZaxBase_t::dumpAttributes()
{
	cout << "#ATTRIBUTE num_bb=" << dec << m_num_bb << endl;
	cout << "#ATTRIBUTE num_bb_instrumented=" << m_num_bb_instrumented << endl;
	cout << "#ATTRIBUTE num_bb_skipped=" << m_num_bb_skipped << endl;
	cout << "#ATTRIBUTE num_bb_skipped_pushjmp=" << m_num_bb_skipped_pushjmp << endl;
	cout << "#ATTRIBUTE num_bb_skipped_nop_padding=" << m_num_bb_skipped_nop_padding << endl;
	cout << "#ATTRIBUTE num_bb_float_instrumentation=" << m_num_bb_float_instrumentation << endl;
	cout << "#ATTRIBUTE num_bb_float_register_saved=" << m_num_bb_float_regs_saved << endl;
	cout << "#ATTRIBUTE graph_optimize=" << boolalpha << m_graph_optimize << endl;
	cout << "#ATTRIBUTE num_bb_skipped_cond_branch=" << m_num_bb_skipped_cbranch << endl;
	cout << "#ATTRIBUTE num_style_collafl=" << m_num_style_collafl << endl;
	cout << "#ATTRIBUTE num_domgraph_blocks_elided=" << m_num_domgraph_blocks_elided << endl;
	cout << "#ATTRIBUTE num_entry_blocks_elided=" << m_num_entry_blocks_elided << endl;
	cout << "#ATTRIBUTE num_exit_blocks_elided=" << m_num_exit_blocks_elided << endl;
	cout << "#ATTRIBUTE num_single_block_function_elided=" << m_num_single_block_function_elided << endl;
	cout << "#ATTRIBUTE num_contexts=" << m_num_contexts << endl;
	cout << "#ATTRIBUTE num_contexts_entry=" << m_num_contexts_entry << endl;
	cout << "#ATTRIBUTE num_contexts_exit=" << m_num_contexts_exit << endl;
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

void ZaxBase_t::addContextSensitivity_Callsite(const ControlFlowGraph_t& cfg)
{
	assert(0);
}

// update calling context hash at entry point
// revert calling context hash on exit 
void ZaxBase_t::addContextSensitivity_Function(const ControlFlowGraph_t& cfg)
{
	bool inserted_before = false;

	// don't bother with single block functions
	if (cfg.getBlocks().size() == 1)
		return;

	m_num_contexts++;
	m_num_contexts_entry++;

	//
	// entry_point
	//      context = prev_context % RANDOM_CONTEXT_ID
	//
	// exit point (returns)
	//      context = prev_context % RANDOM_CONTEXT_ID
	//
	const auto do_insert=[&](Instruction_t* instr, const string& insn_str) -> Instruction_t*
		{
			if (inserted_before)
			{
				instr = insertAssemblyAfter(instr, insn_str);
				return instr;
			}
			else
			{
				insertAssemblyBefore(instr, insn_str);
				inserted_before = true;
				return instr;
			}
		};

	auto compute_hash_chain = [&](ZaflContextId_t contextid, Instruction_t * instr, string reg_context, string reg_temp) -> Instruction_t*
		{
			auto labelid = getLabelId();
			auto tmp = instr;

			// fast way with fixed addresses:
			//         xor [regc], <context_id>
			//
			// inefficient way:
			//      E: mov   regc, QWORD[rel E]
			//         mov   rtmp, [regc]
			//         xor   rtmp, <context_id>
			//         mov [regc], rtmp
			//
			//         
			if (useFixedAddresses())
			{
				const auto xor_context = string("xor WORD [0x") + to_hex_string(getFixedAddressContext()) + "]" + "," + to_string(contextid);
				tmp = do_insert(tmp, xor_context);
			}
			else
			{
				const auto hash_context_reloc = string("E") + to_string(labelid) + ": mov " + reg_context + ", QWORD [rel E" + to_string(labelid) + "]"; 
				tmp = do_insert(tmp, hash_context_reloc);
				create_got_reloc(getFileIR(), m_context_id, tmp);
			
				const auto deref_context = string("mov ") + reg_temp + ", [" + reg_context + "]";
				tmp = do_insert(tmp, deref_context);

				const auto hash_chain = string("xor ") + reg_temp + ", " + to_string(contextid);
				tmp = do_insert(tmp, hash_chain);

				const auto store_context = string("mov [") + reg_context + "]" + "," + reg_temp;
				tmp = do_insert(tmp, store_context);
			}

			return tmp;
		};

	auto add_hash_context_instrumentation = [&](ZaflContextId_t contextid, Instruction_t* i, const bool honor_red_zone)
		{
			inserted_before = false;

			// look for instruction in entry block with at least 1 free reg
			auto reg_context = string("r14");
			auto reg_temp = string("r15");
			bool save_context = true;
			bool save_temp = true;

			if (useFixedAddresses())
			{
				save_context = false;
				save_temp = false;
			}

			const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
			const auto dead_regs = getDeadRegs(i);
			const auto live_flags = dead_regs.find(IRDB_SDK::rn_EFLAGS)==dead_regs.end();
			auto free_regs = getFreeRegs(dead_regs, allowed_regs);

			if (honor_red_zone)
				i = do_insert(i, "lea rsp, [rsp-128]");

			if (save_context && free_regs.size() > 0)
			{
				reg_context = registerToString(FIRSTOF(free_regs));
				free_regs.erase(FIRSTOF(free_regs));
				save_context = false;
			}

			if (save_temp && free_regs.size() > 0)
			{
				reg_temp = registerToString(FIRSTOF(free_regs));
				free_regs.erase(FIRSTOF(free_regs));
				save_temp = false;
			}

			if (save_context)
				i = do_insert(i, "push " + reg_context);

			if (save_temp)
				i = do_insert(i, "push " + reg_temp);

			if (live_flags)
				i = do_insert(i, "pushf");

			// compute new hash chain value
			i = compute_hash_chain(contextid, i, reg_context, reg_temp);

			if (live_flags)
				i = do_insert(i, "popf");

			if (save_temp)
				i = do_insert(i, "pop " + reg_temp);

			if (save_context)
				i = do_insert(i, "pop " + reg_context);

			if (honor_red_zone)
				i = do_insert(i, "lea rsp, [rsp+128]");
		};

	bool honor_red_zone = true;
	if (m_use_stars) 
		honor_red_zone = hasLeafAnnotation(cfg.getFunction());

	auto contextid = getContextId();
	auto entry_block = cfg.getEntry();
	inserted_before = false;
	add_hash_context_instrumentation(contextid, entry_block->getInstructions()[0], honor_red_zone);

	// find all exit blocks with returns
	auto find_exits = BasicBlockSet_t();
	copy_if(ALLOF(cfg.getBlocks()), inserter(find_exits, find_exits.begin()), [entry_block](BasicBlock_t* bb) {
			if (bb == entry_block) return false; 			
			if (!bb->getIsExitBlock()) return false; 			
			const auto last_instruction_index = bb->getInstructions().size() - 1;
			return (bb->getInstructions()[last_instruction_index]->getDisassembly().find("ret")!=string::npos);
		});

	for (const auto &bb: find_exits)
	{
		const auto last_instruction_index = bb->getInstructions().size()-1;
		inserted_before = false;
		add_hash_context_instrumentation(contextid, bb->getInstructions()[last_instruction_index], honor_red_zone);
		m_num_contexts_exit++;
	}
}

void ZaxBase_t::addContextSensitivity(const ControlFlowGraph_t& cfg)
{
	if (m_entry_point && m_entry_point->getFunction())
	{
		cout << "cfg.func: " << cfg.getFunction()->getName() << " ep.func: " << m_entry_point->getFunction()->getName() << endl;
		if (m_entry_point->getFunction()->getName() == cfg.getFunction()->getName())
		{
			cout << "Do not setup calling context in same function as entry point for fork server" << endl;
			return;
		}
	}

	if (getContextSensitivity() == ContextSensitivity_Callsite)
		addContextSensitivity_Callsite(cfg);
	else if (getContextSensitivity() == ContextSensitivity_Function)
		addContextSensitivity_Function(cfg);
	else if (getContextSensitivity() == ContextSensitivity_None)
		return;
	else
		throw;
}


void ZaxBase_t::addLibZaflIntegration()
{
	const auto ptrsize      = getFileIR()->getArchitectureBitWidth()/8;
	const auto raw_contents = reinterpret_cast<const char*>(&m_trace_map_fixed_addr);

	auto sa       = getFileIR()->addNewAddress(getFileIR()->getFile()->getFileID(), 0);
	auto ea       = getFileIR()->addNewAddress(getFileIR()->getFile()->getFileID(), ptrsize-1);
	auto contents = string(raw_contents, ptrsize);

	(void)getFileIR()->addNewDataScoop("libZaflIntegration", sa, ea, nullptr, 6, false, contents );
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
	if (m_breakupCriticalEdges)
	{
		CriticalEdgeBreaker_t ceb(getFileIR(), m_blacklist, m_verbose);
		cout << "#ATTRIBUTE num_bb_extra_blocks=" << ceb.getNumberExtraNodes() << endl;

		getFileIR()->setBaseIDS();
		getFileIR()->assembleRegistry();
	}

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
	const auto sortedFuncs=set<Function_t*, BaseIDSorter>( ALLOF(getFileIR()->getFunctions()));
	for(auto f :  sortedFuncs)
	{
		if (f == nullptr )       continue;
		// skip instrumentation for blacklisted functions 
		if (isBlacklisted(f))    continue;
		// skip if function has no entry point
		if (!f->getEntryPoint()) continue;

		bool honorRedZone = true;
		if (m_use_stars) 
			honorRedZone = hasLeafAnnotation(f);

		const auto cfgp = ControlFlowGraph_t::factory(f);
		const auto &cfg = *cfgp;
		const auto num_blocks_in_func = cfg.getBlocks().size();
		m_num_bb += num_blocks_in_func;

		// skip single-block functions that are not indirectly called
		if (num_blocks_in_func == 1 && !f->getEntryPoint()->getIndirectBranchTargetAddress())
		{
			m_num_single_block_function_elided++;
			m_num_bb_skipped++;
			continue;
		}

		const auto dom_graphp=DominatorGraph_t::factory(cfgp.get());
		const auto has_domgraph_warnings = dom_graphp -> hasWarnings();  

		const auto entry_block = cfg.getEntry();
		auto keepers = getBlocksToInstrument(cfg);

		if (m_verbose)
			cout << "num blocks to keep (baseline): " << keepers.size() << endl;

		if(has_domgraph_warnings)
		{
			if(m_verbose)
			{
				cout << " Domgraph has warnings, eliding domgraph filter" << endl;
				cout << " And the domgraph is: " << endl;
				cout << *dom_graphp << endl;
			}
		}
		filterBlocksByDomgraph(keepers,dom_graphp.get());

		if (m_verbose)
			cout << "num blocks to keep (after filter dom): " << keepers.size() << " / " << cfgp->getBlocks().size() << endl;

		if (m_graph_optimize)
		{
			filterEntryBlock(keepers, entry_block);
			if (m_verbose)
				cout << "num blocks to keep (after filter entry): " << keepers.size() << endl;

			filterExitBlocks(keepers);
			if (m_verbose)
				cout << "num blocks to keep (after filter exits): " << keepers.size() << endl;
		}

		filterPaddingNOP(keepers);

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
			if (m_graph_optimize               && 
			    bb->getPredecessors().size() == 1 && 
			    !bb->getInstructions()[0]->getIndirectBranchTargetAddress()
			   )
			{
				if (m_verbose) 
					cout << "Doing collAfl style" << endl;
				collAflSingleton = true;
				m_num_style_collafl++;
			}

			instrumentBasicBlock(bb, honorRedZone, collAflSingleton);
		}

		m_num_bb_instrumented += keepers.size();
		m_num_bb_skipped += (num_blocks_in_func - keepers.size());

		if (getContextSensitivity() != ContextSensitivity_None)
		{
			// this handles inserting the calling context sensitivity value
			// at entry and exits of functions
 			getFileIR()->assembleRegistry();
		 	getFileIR()->setBaseIDS();
			auto cs_cfg=ControlFlowGraph_t::factory(f);	
			addContextSensitivity(*cs_cfg);
		}

		addLibZaflIntegration();

		if (m_verbose)
		{
 			getFileIR()->assembleRegistry();
		 	getFileIR()->setBaseIDS();
			cout << "Post transformation CFG for " << f->getName() << ":" << endl;
			auto post_cfg=ControlFlowGraph_t::factory(f);	
			cout << *post_cfg << endl;
		}

		cout << "Function " << f->getName() << ":  " << dec << keepers.size() << "/" << num_blocks_in_func << " basic blocks instrumented." << endl;
	};


	teardown();

	return 1;
}

