#include "zuntracer.hpp"
#include "critical_edge_breaker.hpp"

using namespace Zafl;

ZUntracer_t::ZUntracer_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl, bool p_verbose) : Zax_t(p_dbinterface, p_variantIR, p_forkServerEntryPoint, p_exitPoints, p_use_stars, p_autozafl, p_verbose)
{
	m_blockid = 0;
}

zafl_blockid_t ZUntracer_t::get_blockid(const unsigned p_max) 
{
	assert (m_blockid < p_max);
	return m_blockid++;
}

void ZUntracer_t::afl_instrument_bb(Instruction_t *p_inst, const bool p_redZoneHint, const bool p_collafl_optimization)
{
	assert(p_inst);

	/*
	Original afl instrumentation:
	        block_id = <random>;
	        zafl_trace_bits[zafl_prev_id ^ block_id]++;
		zafl_prev_id = block_id >> 1;     

	Zuntracer instrumentation (simple block coverage)
		zafl_trace_bits[block_id] = 1;
	*/

	char buf[8192];
	auto tmp = p_inst;
	char *reg_trace_map = NULL;
	Instruction_t *orig = nullptr;
	auto found_tracemap_free_register = false;

	// 1st instruction in block record is the new entry point of the block (p_inst)
	// 2nd instruction in block record is where the instruction at the old entry point is now at (orig)
	BBRecord_t block_record;

	block_record.push_back(p_inst);
	
	const auto blockid = get_blockid();
	const auto labelid = get_labelid(); 

	cout << "working with blockid: " << blockid << " labelid: " << labelid << endl;

	if (m_use_stars) 
	{
		auto regset = get_dead_regs(p_inst, m_stars_analysis_engine.getAnnotations());
		for (auto r : regset)
		{
			if (r == rn_RCX)
			{
				// favor rcx for tracemap by convention
				reg_trace_map = strdup("rcx");
				found_tracemap_free_register = true;
			}
		}

		// rcx not available, try to find another free register
		if (!found_tracemap_free_register) 
		{
			const RegisterSet_t allowed_regs = {rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15};

			auto free_regs = get_free_regs(regset, allowed_regs);
			if (free_regs.size() > 0)
			{
				auto r = *free_regs.begin();
				reg_trace_map = strdup(Register::toString(r).c_str());
				found_tracemap_free_register = true;
			}
		}
	}

	auto inserted_before = false;
	auto honorRedZone = p_redZoneHint;

	// if we have a free register, we don't muck with the stack ==> no need to honor red zone
	if (found_tracemap_free_register)
	{
		honorRedZone = false;
	}

	if (honorRedZone) 
	{
		orig = insertAssemblyBefore(tmp, "lea rsp, [rsp-128]");
		inserted_before = true;
		block_record.push_back(orig);
	}

	// we did not find a free register, save rcx and then use it for the tracemap
	if (!found_tracemap_free_register)
	{
		if (inserted_before) 
		{
			tmp = insertAssemblyAfter(tmp, "push rcx");
			block_record.push_back(tmp);
		}
		else
		{
			orig = insertAssemblyBefore(tmp, "push rcx");
			inserted_before = true;
			block_record.push_back(orig);
		}

		reg_trace_map = strdup("rcx");
	}

	assert(reg_trace_map);

	// load address trace map into rcx
	sprintf(buf, "T%d: mov  %s, QWORD [rel T%d]", labelid, reg_trace_map, labelid); 
	if (inserted_before)
	{
		tmp = insertAssemblyAfter(tmp, buf);
		block_record.push_back(tmp);
	}
	else
	{
		orig = insertAssemblyBefore(tmp, buf);
		block_record.push_back(orig);
	}
	create_got_reloc(getFileIR(), m_trace_map, tmp);

	// update trace map
	sprintf(buf,"mov %s, [%s]", reg_trace_map, reg_trace_map);
	tmp = insertAssemblyAfter(tmp, buf);
	block_record.push_back(tmp);

	sprintf(buf,"mov BYTE [%s+0x%x], 1", reg_trace_map, blockid);
	tmp = insertAssemblyAfter(tmp, buf);
	block_record.push_back(tmp);

	// restore register
	if (!found_tracemap_free_register)
	{
		tmp = insertAssemblyAfter(tmp, "pop rcx");
		block_record.push_back(tmp);
	}
	
	// red zone
	if (honorRedZone) 
	{
		tmp = insertAssemblyAfter(tmp, "lea rsp, [rsp+128]");
		block_record.push_back(tmp);
	}

	// record modified blocks, indexed by the block id
	assert(orig);
	assert(tmp->GetFallthrough());
	assert(orig == tmp->GetFallthrough()); // sanity check invariant of transform

	m_modifiedBlocks[blockid] = block_record;

	free(reg_trace_map);
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

		getFileIR()->SetBaseIDS();
		getFileIR()->AssembleRegistry();
	}

	return Zax_t::execute();
}

