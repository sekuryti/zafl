#include "zuntracer.hpp"
#include "critical_edge_breaker.hpp"

using namespace Zafl;
using namespace MEDS_Annotation;

ZUntracer_t::ZUntracer_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl, bool p_verbose) : Zax_t(p_dbinterface, p_variantIR, p_forkServerEntryPoint, p_exitPoints, p_use_stars, p_autozafl, p_verbose)
{
	m_blockid = 0;
}

zafl_blockid_t ZUntracer_t::get_blockid(const unsigned p_max) 
{
//	assert (m_blockid < p_max);
//	@todo: issue warning when wrapping around
	m_blockid = (m_blockid+1) % p_max;
	return m_blockid;
}

void ZUntracer_t::afl_instrument_bb(Instruction_t *p_inst, const bool p_redZoneHint, const bool p_collafl_optimization)
{
	assert(p_inst);

	const auto trace_map_fixed_addr       = getenv("ZAFL_TRACE_MAP_FIXED_ADDRESS");
	const auto do_fixed_addr_optimization = (trace_map_fixed_addr!=nullptr);

	if (do_fixed_addr_optimization)
		_afl_instrument_bb_fixed(p_inst, trace_map_fixed_addr);
	else
		_afl_instrument_bb(p_inst, p_redZoneHint);
}

void ZUntracer_t::_afl_instrument_bb_fixed(Instruction_t *p_inst, char* p_tracemap_addr)
{
	// 1st instruction in block record is the new entry point of the block (p_inst)
	// 2nd instruction in block record is where the instruction at the old entry point is now at (orig)
	const auto blockid = get_blockid();
	BBRecord_t block_record;
	block_record.push_back(p_inst);

	// e.g.: mov BYTE [ 0x10000 + blockid ], 0x1
	const auto s = string("mov BYTE [") + p_tracemap_addr + "+" + to_string(blockid) + "], 0x1";
	const auto orig = insertAssemblyBefore(p_inst, s);
	block_record.push_back(orig);

	m_modifiedBlocks[blockid] = block_record;
}

void ZUntracer_t::_afl_instrument_bb(Instruction_t *p_inst, const bool p_redZoneHint)
{
	/*
	Original afl instrumentation:
	        block_id = <random>;
	        zafl_trace_bits[zafl_prev_id ^ block_id]++;
		zafl_prev_id = block_id >> 1;     

	Zuntracer instrumentation (simple block coverage)
		zafl_trace_bits[block_id] = 1;
	*/

	auto tmp = p_inst;
	auto tracemap_reg = string();
	auto found_tracemap_free_register = false;

	// 1st instruction in block record is the new entry point of the block (p_inst)
	// 2nd instruction in block record is where the instruction at the old entry point is now at (orig)
	BBRecord_t block_record;

	block_record.push_back(p_inst);
	
	const auto blockid = get_blockid();
	const auto labelid = get_labelid(); 

	if (m_verbose)
		cout << "working with blockid: " << blockid << " labelid: " << labelid << endl;

	if (m_use_stars) 
	{
		const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
		const auto dead_regs = get_dead_regs(p_inst, m_stars_analysis_engine.getAnnotations());
		const auto free_regs = get_free_regs(dead_regs, allowed_regs);
		if (free_regs.size() > 0)
		{
			auto r = *free_regs.begin();
			tracemap_reg = Register::toString(r);
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
	string load_trace_map = "T" + to_string(labelid) + ": mov " + tracemap_reg + " , QWORD [ rel T" + to_string(labelid) + "]";
	do_insert(load_trace_map);
	create_got_reloc(getFileIR(), m_trace_map, tmp);

	// update trace map: e.g.:              mov rcx, [rcx]
	do_insert("mov " + tracemap_reg + ", [" + tracemap_reg + "]");

	// set counter to 1:                    mov BYTE [rcx+1234], 1
	do_insert("mov BYTE [" + tracemap_reg + "+" + to_string(blockid) + "]");

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

set<libIRDB::BasicBlock_t*> ZUntracer_t::getBlocksToInstrument(libIRDB::ControlFlowGraph_t &cfg)
{
	static int bb_z_debug_id=-1;

	if (m_verbose)
		cout << cfg << endl;

	auto keepers = set<libIRDB::BasicBlock_t*>();

	for (auto &bb : cfg.GetBlocks())
	{
		bb_z_debug_id++;

		// already marked as a keeper
		if (keepers.find(bb) != keepers.end())
			continue;
 
		// if whitelist specified, only allow instrumentation for functions/addresses in whitelist
		if (m_whitelist.size() > 0) 
		{
			if (!isWhitelisted(bb->GetInstructions()[0]))
			{
				continue;
			}
		}

		if (isBlacklisted(bb->GetInstructions()[0]))
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
		if (bb->GetInstructions()[0]->getBaseID() < 0)
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
		if (m_bb_graph_optimize)
		{
			if (bb->GetSuccessors().size() == 2 && bb->EndsInConditionalBranch())
			{
				m_num_bb_skipped_cbranch++;
				continue;
			}
		}

		keepers.insert(bb);
	}
	return keepers;
}

// 
// breakup critical edges
// use block-level instrumentation
// 
int ZUntracer_t::execute()
{
	if (m_breakupCriticalEdges)
	{
		CriticalEdgeBreaker_t ceb(getFileIR(), m_verbose);
		cout << "#ATTRIBUTE num_bb_extra_blocks=" << ceb.getNumberExtraNodes() << endl;

		getFileIR()->setBaseIDS();
		getFileIR()->assembleRegistry();
	}

	return Zax_t::execute();
}

