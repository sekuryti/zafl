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

#include <iostream>
#include <algorithm>
#include <irdb-cfg>

#include "laf.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Laf;

#define ALLOF(a) begin(a),end(a)
#define FIRSTOF(a) (*(begin(a)))

Laf_t::Laf_t(pqxxDB_t &p_dbinterface, FileIR_t *p_variantIR, bool p_verbose)
	:
	Transform_t(p_variantIR),
	m_dbinterface(p_dbinterface),
	m_verbose(p_verbose)
{
	m_trace_compare = true;
	m_trace_div = true;

	auto deep_analysis=DeepAnalysis_t::factory(getFileIR());
	dead_registers = deep_analysis->getDeadRegisters();

	m_blacklist.insert(".init");
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

	m_num_cmp = 0;
	m_num_cmp_instrumented = 0;
	m_num_div = 0;
	m_num_div_instrumented = 0;

	m_skip_easy_val = 0;
	m_skip_byte = 0;
	m_skip_word = 0;
	m_skip_qword = 0;
	m_skip_relocs = 0;
	m_skip_stack_access = 0;
	m_skip_no_free_regs = 0;
	m_skip_unknown = 0;
}

// return dead registers for instruction
RegisterSet_t Laf_t::getDeadRegs(Instruction_t* p_insn) const
{
	auto it = dead_registers -> find(p_insn);
	if(it != dead_registers->end())
		return it->second;
	return RegisterSet_t();
}

// return intersection of candidates and allowed general-purpose registers
RegisterSet_t Laf_t::getFreeRegs(const RegisterSet_t& p_candidates, const RegisterSet_t& p_allowed) const
{
	RegisterIDSet_t free_regs;
	set_intersection(ALLOF(p_candidates), ALLOF(p_allowed), std::inserter(free_regs,free_regs.begin()));
	return free_regs;
}

bool Laf_t::isBlacklisted(Function_t *p_func) const
{
	// leverage cases where we have function name (i.e., non-stripped binary)
	return (p_func->getName()[0] == '.' || 
	        p_func->getName().find("@plt") != string::npos ||
	        p_func->getName().find("_libc_") != string::npos ||
	        m_blacklist.find(p_func->getName())!=m_blacklist.end());
}

void Laf_t::setTraceCompare(bool p_val)
{
	m_trace_compare = p_val;
}

bool Laf_t::getTraceCompare() const
{
	return m_trace_compare;
}

void Laf_t::setTraceDiv(bool p_val)
{
	m_trace_div = p_val;
}

bool Laf_t::getTraceDiv() const
{
	return m_trace_div;
}

bool Laf_t::getFreeRegister(Instruction_t* p_instr, string& p_freereg, RegisterSet_t p_allowed_regs)
{
	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;
	auto save_tmp = true;

	// register in instruction cannot be used as a free register
	if (d.getOperand(0)->isRegister())
	{
		const auto r = d.getOperand(0)->getString();
		const auto reg = strToRegister(r);
		p_allowed_regs.erase(reg);
		p_allowed_regs.erase(convertRegisterTo64bit(reg));
	}

	const auto dead_regs = getDeadRegs(p_instr);
	auto free_regs = getFreeRegs(dead_regs, p_allowed_regs);

	if (m_verbose)
	{
		cout << "STARS says num dead registers: " << dead_regs.size();
		cout << "  ";
		for (auto r : dead_regs)
			cout << registerToString(r) << " ";
		cout << endl;
		cout << "STARS says num free registers: " << free_regs.size();
		cout << "  ";
		for (auto r : free_regs)
			cout << registerToString(r) << " ";
		cout << endl;
	}

	if (free_regs.size() > 0)
	{
		const auto first_free_register = FIRSTOF(free_regs);
		free_regs.erase(first_free_register);
		p_freereg = registerToString(first_free_register);
		save_tmp = false;
	}
	else
	{
		const auto disasm = p_instr->getDisassembly();
		if (disasm.find("r11")==string::npos)
			p_freereg = "r11";
		else if (disasm.find("r12")==string::npos)
			p_freereg = "r12";
		else if (disasm.find("r13")==string::npos)
			p_freereg = "r13";
		else if (disasm.find("r14")==string::npos)
			p_freereg = "r14";
		else if (disasm.find("r15")==string::npos)
			p_freereg = "r15";
		save_tmp = true;
	}

	return save_tmp;
}

// for each function, search and instrument cmp
int Laf_t::doTraceCompare()
{
	for(auto func : getFileIR()->getFunctions())
	{
		auto to_trace_compare = vector<Instruction_t*>(); 
		if (isBlacklisted(func))
			continue;

		if (m_verbose) cout << endl << "Handling function: " << func->getName() << endl;
		for(auto i : func->getInstructions())
		{
			if (i->getBaseID() <= 0) continue;
			const auto dp = DecodedInstruction_t::factory(i);
			const auto &d = *dp;
			if (d.getMnemonic()!="cmp") continue;
			if (d.getOperands().size()!=2) continue;
			if (!d.getOperand(1)->isConstant()) continue;

			if (d.getOperand(0)->getArgumentSizeInBytes()==1)
			{
				m_skip_byte++;
				continue;
		 	}

			m_num_cmp++;

			// we now have a cmp instruction to trace
			// we handle 2, 4, 8 byte compares
			if (d.getOperand(0)->isRegister() || d.getOperand(0)->isMemory())
				to_trace_compare.push_back(i);
			else
				m_skip_unknown++;
		};

		// split comparisons
		for(auto c : to_trace_compare)
		{
			if (getenv("LAF_LIMIT_END"))
			{
				auto debug_limit_end = static_cast<unsigned>(atoi(getenv("LAF_LIMIT_END")));
				if (m_num_cmp_instrumented >= debug_limit_end)
					break;
			}
			const auto s = c->getDisassembly();
			const auto dp = DecodedInstruction_t::factory(c);
			const auto &d = *dp;
			if (traceBytesNested(c, d.getImmediate()))
			{
				if (m_verbose)
				{
					cout << "success for " << s << endl;
				}
				m_num_cmp_instrumented++;
			}
		}

		if (m_verbose) 
		{
 			getFileIR()->assembleRegistry();
		 	getFileIR()->setBaseIDS();
			cout << "Post transformation CFG for " << func->getName() << ":" << endl;
			auto post_cfg=ControlFlowGraph_t::factory(func);	
			cout << *post_cfg << endl;
		}
	};

	return 1;	 // true means success

}

// for each function, search and instrument div
int Laf_t::doTraceDiv()
{
	for(auto func : getFileIR()->getFunctions())
	{
		auto to_trace_div = vector<Instruction_t*>(); 
		if (isBlacklisted(func))
			continue;

		if (m_verbose)
			cout << endl << "Handling function: " << func->getName() << endl;

		for(auto i : func->getInstructions())
		{
			if (i->getBaseID() <= 0) continue;
			const auto dp = DecodedInstruction_t::factory(i);
			const auto &d = *dp;
			if (d.getMnemonic() != "div" && d.getMnemonic() !="idiv") continue;
			if (d.getOperands().size()!=1) continue;
			if (d.getOperand(0)->getArgumentSizeInBytes()==1)
			{
				m_skip_byte++;
				continue;
			}

			m_num_div++;

			// we handle 2, 4, 8 byte div
			if (d.getOperand(0)->isRegister() || d.getOperand(0)->isMemory())
				to_trace_div.push_back(i);
		};

		// Trace the div instruction
		for(auto c : to_trace_div)
		{
			if (getenv("LAF_LIMIT_END"))
			{
				auto debug_limit_end = static_cast<unsigned>(atoi(getenv("LAF_LIMIT_END")));
				if (m_num_div_instrumented >= debug_limit_end)
					break;
			}
			const auto s = c->getDisassembly();
			if (traceBytesNested(c, 0))
			{
				if (m_verbose)
					cout << "success for " << s << endl;
				m_num_div_instrumented++;
			}
		}

	};

	return 1;	 // true means success
}

//
// Break up compares and divs into nested byte compares
// Then reuse the original compare/div instruction
//
bool Laf_t::traceBytesNested(Instruction_t *p_instr, int64_t p_immediate)
{
	// bail out, there's a reloc here
	if (p_instr->getRelocations().size() > 0)
	{
		m_skip_relocs++;
		return false;
	}

	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;

	const auto num_bytes = d.getOperand(0)->getArgumentSizeInBytes();
	if (num_bytes!=2 && num_bytes==4 && num_bytes==8)
		throw std::domain_error("laf transform only handles 2,4,8 byte compares/div");

	union constant_union {
		int64_t immediate;
		uint8_t b[8];
	};

	constant_union K;
        K.immediate = p_immediate;

	if (m_verbose)
	{
		cout << "Immediate = 0x" << hex << p_immediate << endl;
		cout << "K[] = ";
		for (unsigned i = 0; i < 8; ++i)
		{
			cout << "0x" << hex << (unsigned)K.b[i] << " ";
		}
		cout << endl;
	}

	//
	// start instrumenting below
	//
	
	// [rsp-128] used to save register (if we can't find a free register)
	auto s = string();
	auto t = p_instr;
	auto traced_instr = p_instr;

	// copy value to compare into free register
	auto save_tmp = true;
	auto free_reg8 = string("");
	const auto div = d.getMnemonic()== "div" || d.getMnemonic()=="idiv";
	if (div)
		save_tmp = getFreeRegister(p_instr, free_reg8, RegisterSet_t({rn_RBX, rn_RCX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));
	else
		save_tmp = getFreeRegister(p_instr, free_reg8, RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));
	const auto free_reg1 = registerToString(convertRegisterTo8bit(strToRegister(free_reg8)));
	const auto free_reg2 = registerToString(convertRegisterTo16bit(strToRegister(free_reg8)));
	const auto free_reg4 = registerToString(convertRegisterTo32bit(strToRegister(free_reg8)));
	if(free_reg8.empty()) throw;
	
	if (m_verbose)
	{
		cout << "temporary register needs to be saved: " << boolalpha << save_tmp << endl;
		cout << "temporary register is: " << free_reg8 << endl;
	}

	// stash away original register value (if needed)
	const auto red_zone_reg = string(" qword [rsp-128] ");
	if (save_tmp)
	{
		s = "mov " + red_zone_reg + ", " + free_reg8;
		traced_instr = insertAssemblyBefore(traced_instr, s);
		cout << "save tmp in red zone: " << s << endl;
	}

	// copy value into free register
	if (d.getOperand(0)->isRegister())
	{
		auto source_reg = d.getOperand(0)->getString();
		if (num_bytes == 8)
		{
			source_reg = registerToString(convertRegisterTo64bit(strToRegister(source_reg)));
			s = "mov " + free_reg8 + ", " + source_reg;
		}
		else if (num_bytes == 4)
		{
			source_reg = registerToString(convertRegisterTo32bit(strToRegister(source_reg)));
			s = "mov " + free_reg4 + ", " + source_reg;
		}
		else
		{
			source_reg = registerToString(convertRegisterTo16bit(strToRegister(source_reg)));
			s = "mov " + free_reg2 + ", " + source_reg;
		}
	}
	else
	{
		const auto memopp = d.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		if (num_bytes == 8)
			s = "mov " + free_reg8 + ", qword [ " + memop_str + "]"; 
		else if (num_bytes == 4)
			s = "mov " + free_reg4 + ", dword [ " + memop_str + "]"; 
		else if (num_bytes == 2)
			s = "mov " + free_reg2 + ", word [ " + memop_str + "]"; 
	}

	if (t == traced_instr)
		traced_instr = insertAssemblyBefore(t, s);
	else
		t = insertAssemblyAfter(t, s);
	cout << s << endl;

	//
	// free_reg has the value
	// K.b[] has the bytes
	//
	for (unsigned i = 0; i < num_bytes; ++i)
	{
		s = "cmp " + free_reg1 + ", " + to_string(K.b[i]);
		t = insertAssemblyAfter(t, s);
		cout << s << endl;

		s = "jne 0";  
		t = insertAssemblyAfter(t, s);
		t->setTarget(traced_instr);
		cout << s << endl;

		if (i != num_bytes-1)
		{
			s = "shr " + free_reg8 + ", 8";
			t = insertAssemblyAfter(t, s);
			cout << s << endl;
		}
	}

	s = "jmp 0";
	t = insertAssemblyAfter(t, s);
	t->setTarget(traced_instr);
	cout << s << endl;

	if (save_tmp)
	{
		s = "mov " + free_reg8 + ", " + red_zone_reg;
		insertAssemblyBefore(traced_instr, s);
		cout << "restore tmp from red zone:" << s << endl;
	}

	return true;
}

int Laf_t::execute()
{
	if (getTraceCompare())
		doTraceCompare();

	if (getTraceDiv())
		doTraceDiv();

	cout << "#ATTRIBUTE num_cmp_patterns=" << dec << m_num_cmp << endl;
	cout << "#ATTRIBUTE num_cmp_instrumented=" << dec << m_num_cmp_instrumented << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_easyval=" << m_skip_easy_val << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_byte=" << m_skip_byte << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_word=" << m_skip_word << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_qword=" << m_skip_qword << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_relocs=" << m_skip_relocs << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_stack_access=" << m_skip_stack_access << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_no_regs=" << m_skip_no_free_regs << endl;
	cout << "#ATTRIBUTE num_cmp_skipped_unknown=" << m_skip_unknown << endl;
	cout << "#ATTRIBUTE num_div=" << dec << m_num_div << endl;
	cout << "#ATTRIBUTE num_div_instrumented=" << dec << m_num_div_instrumented << endl;

	return 1;
}

