// @HEADER_LANG C++ 
// @HEADER_COMPONENT zafl
// @HEADER_BEGIN
/*
* Copyright (c) 2018-2019 Zephyr Software LLC
*
* This file may be used and modified for non-commercial purposes as long as
* all copyright, permission, and nonwarranty notices are preserved.
* Redistribution is prohibited without prior written consent from Zephyr
* Software.
*
* Please contact the authors for restrictions applying to commercial use.
*
* THIS SOURCE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
* MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*
* Author: Zephyr Software
* e-mail: jwd@zephyr-software.com
* URL   : http://www.zephyr-software.com/
*
* This software was developed with SBIR funding and is subject to SBIR Data Rights, 
* as detailed below.
*
* SBIR DATA RIGHTS
*
* Contract No. __FA8750-17-C-0295___________________________.
* Contractor Name __Zephyr Software LLC_____________________.
* Address __4826 Stony Point Rd, Barboursville, VA 22923____.
*
*/

// @HEADER_END
#include "zuntracer.hpp"

#define ALLOF(a) begin(a),end(a)

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

ZUntracer_t::ZUntracer_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl) : ZaxBase_t(p_dbinterface, p_variantIR, p_forkServerEntryPoint, p_exitPoints, p_use_stars, p_autozafl)
{
}

void ZUntracer_t::instrumentBasicBlock(BasicBlock_t *p_bb, const bool p_redZoneHint, const bool p_collafl_optimization)
{
	if (!p_bb) throw;

	const auto trace_map_fixed_addr       = getenv("ZAFL_TRACE_MAP_FIXED_ADDRESS");
	const auto do_fixed_addr_optimization = (trace_map_fixed_addr!=nullptr);

	if (do_fixed_addr_optimization)
		_instrumentBasicBlock_fixed(p_bb, trace_map_fixed_addr);
	else
		_instrumentBasicBlock(p_bb, p_redZoneHint);
}

// Highly efficient instrumentation using known address for tracemap
void ZUntracer_t::_instrumentBasicBlock_fixed(BasicBlock_t *p_bb, char* p_tracemap_addr)
{
	const auto &bb_instructions = p_bb->getInstructions();
	// 1st instruction in block record is the new entry point of the block (instr)
	// 2nd instruction in block record is where the instruction at the old entry point is now at (orig)
	auto instr = getInstructionToInstrument(p_bb);
	if (!instr) throw;

	const auto blockid = getBlockId();
	BBRecord_t block_record;
	block_record.insert(end(block_record), ALLOF(bb_instructions));

	// e.g.: mov BYTE [ 0x10000 + blockid ], 0x1
	const auto s = string("mov BYTE [") + p_tracemap_addr + "+" + to_string(blockid) + "], 0x1";
	const auto orig = insertAssemblyBefore(instr, s);
	block_record.push_back(orig);

	m_modifiedBlocks[blockid] = block_record;
}

void ZUntracer_t::_instrumentBasicBlock(BasicBlock_t *p_bb, const bool p_redZoneHint)
{
	/*
	Original afl instrumentation:
	        block_id = <random>;
	        zafl_trace_bits[zafl_prev_id ^ block_id]++;
		zafl_prev_id = block_id >> 1;     

	Zuntracer instrumentation (simple block coverage)
		zafl_trace_bits[block_id] = 1;
	*/

	const auto num_free_regs_desired = 1;
	auto instr = getInstructionToInstrument(p_bb, num_free_regs_desired);
	if (!instr) throw;

	auto tmp = instr;
	auto tracemap_reg = string();
	auto found_tracemap_free_register = false;

	// 1st instruction in block record is the new entry point of the block (instr)
	// 2nd instruction in block record is where the instruction at the old entry point is now at (orig)
	BBRecord_t block_record;

	block_record.push_back(instr);
	
	const auto blockid = getBlockId();
	const auto labelid = getLabelId(); 

	if (m_verbose)
		cout << "working with blockid: " << blockid << " labelid: " << labelid << endl;

	if (m_use_stars) 
	{
		const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
		const auto dead_regs = getDeadRegs(instr);
		const auto free_regs = getFreeRegs(dead_regs, allowed_regs);
		if (free_regs.size() > 0)
		{
			auto r = *free_regs.begin();
			tracemap_reg = registerToString(r);
			found_tracemap_free_register = true;
		}
	}

	auto inserted_before = false;
	auto honorRedZone = p_redZoneHint;

	const auto do_insert=[&](const string& insn_str) -> void
		{
			if (inserted_before)
			{
				tmp = insertAssemblyAfter(tmp, insn_str);
				block_record.push_back(tmp);
			}
			else
			{
				const auto orig = insertAssemblyBefore(tmp, insn_str);
				inserted_before = true;
				block_record.push_back(orig);
			}
		};


	// if we have a free register, we don't muck with the stack ==> no need to honor red zone
	if (found_tracemap_free_register)
	{
		honorRedZone = false;
	}

	if (honorRedZone) 
	{
		do_insert("lea rsp, [rsp-128]");
	}

	// we did not find a free register, save rcx and then use it for the tracemap
	if (!found_tracemap_free_register)
	{
		tracemap_reg = "rcx";
		do_insert("push " + tracemap_reg);
	}

	assert(tracemap_reg.size()>0);

	// load address trace map into rcx:     T123: mov rcx, QWORD [rel T123]
	const auto load_trace_map = "T" + to_string(labelid) + ": mov " + tracemap_reg + " , QWORD [ rel T" + to_string(labelid) + "]";
	do_insert(load_trace_map);
	create_got_reloc(getFileIR(), m_trace_map, tmp);

	// update trace map: e.g.:              mov rcx, [rcx]
	do_insert("mov " + tracemap_reg + ", [" + tracemap_reg + "]");

	// set counter to 1:                    mov BYTE [rcx+1234], 1
	do_insert("mov BYTE [" + tracemap_reg + "+" + to_string(blockid) + "], 1");

	// restore register
	if (!found_tracemap_free_register)
	{
		do_insert("pop " + tracemap_reg);
	}
	
	// red zone
	if (honorRedZone) 
	{
		do_insert("lea rsp, [rsp+128]");
	}

	m_modifiedBlocks[blockid] = block_record;
}

set<BasicBlock_t*> ZUntracer_t::getBlocksToInstrument(ControlFlowGraph_t &cfg)
{
	static int bb_z_debug_id=-1;

	if (m_verbose)
		cout << cfg << endl;

	auto keepers = set<BasicBlock_t*>();

	for (auto &bb : cfg.getBlocks())
	{
		bb_z_debug_id++;

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
			if (bb_z_debug_id < atoi(getenv("ZAFL_LIMIT_BEGIN")))
				continue;	
		}

		// debugging support
		if (getenv("ZAFL_LIMIT_END"))
		{
			if (bb_z_debug_id >= atoi(getenv("ZAFL_LIMIT_END"))) 
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

		// optimization:
		//    @warning @todo:
		//    very experimental!
		//    elide instrumentation for conditional branches
		//
		if (m_graph_optimize)
		{
			if (bb->getSuccessors().size() == 2 && bb->endsInConditionalBranch())
			{
				m_num_bb_skipped_cbranch++;
				continue;
			}
		}

		keepers.insert(bb);
	}
	return keepers;
}
