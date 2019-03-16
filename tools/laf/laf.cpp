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
#include <iomanip>
#include <irdb-cfg>

#include "laf.hpp"


using namespace std;
using namespace IRDB_SDK;
using namespace Laf;
using namespace MEDS_Annotation;

#define ALLOF(a) begin(a),end(a)
#define FIRSTOF(a) (*(begin(a)))

Laf_t::Laf_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose)
	:
	Transform_t(p_variantIR),
	m_dbinterface(p_dbinterface),
	m_verbose(p_verbose)
{
	m_trace_compare = true;
	m_trace_div = true;

	auto deep_analysis=DeepAnalysis_t::factory(getFileIR());
	leaf_functions = deep_analysis->getLeafFunctions();
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

RegisterSet_t Laf_t::getDeadRegs(Instruction_t* insn) const
{
	auto it = dead_registers -> find(insn);
	if(it != dead_registers->end())
		return it->second;
	return RegisterSet_t();
}

// return intersection of candidates and allowed general-purpose registers
RegisterSet_t Laf_t::getFreeRegs(const RegisterSet_t& candidates, const RegisterSet_t& allowed) const
{
	RegisterIDSet_t free_regs;
	set_intersection(ALLOF(candidates), ALLOF(allowed), std::inserter(free_regs,free_regs.begin()));
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

bool Laf_t::hasLeafAnnotation(Function_t* fn) const
{
	auto it = leaf_functions -> find(fn);
	return (it != leaf_functions->end());
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
		const auto reg = Register::getRegister(r);
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

			// XXX DEBUG
			if (d.getOperand(0)->getArgumentSizeInBytes()>=4)
				continue;

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

			if (d.getOperand(0)->getArgumentSizeInBytes()==2)
			{
				if (traceBytes2(c, d.getImmediate()))
				{
					if (m_verbose) cout << "success for " << s << endl;
					m_num_cmp_instrumented++;
				}
			}
			else if (traceBytes48(c, d.getOperand(0)->getArgumentSizeInBytes(), d.getImmediate()))
			{
				if (m_verbose) cout << "success for " << s << endl;
				m_num_cmp_instrumented++;
			}
		}

	};

	return 1;	 // true means success
}

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
			const auto dp = DecodedInstruction_t::factory(c);
			const auto &d = *dp;
			if (d.getOperand(0)->getArgumentSizeInBytes()==2)
			{
				if (traceBytes2(c, d.getImmediate()))
				{
					if (m_verbose) cout << "success for " << s << endl;
					m_num_div_instrumented++;
				}
			}
			else if (traceBytes48(c, d.getOperand(0)->getArgumentSizeInBytes(), 0))
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
// orig:
//      cmp ax, K
//      idiv ax
//
// [save_reg]
// [save_flags]
// cmp al, K>>2   cmp ax, word [...] >> 2
// idiv al
// [restore_reg]
// jne orig
// jmp orig
//
// orig:
//      cmp ax, K
//
bool Laf_t::traceBytes2(Instruction_t *p_instr, const uint32_t p_immediate)
{
	// bail out, there's a reloc here
	if (p_instr->getRelocations().size() > 0)
	{
		m_skip_relocs++;
		return false;
	}

	const auto lower_byte = p_immediate & 0x000000FF; // we only need to compare against lower byte

	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;

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
	const auto free_reg1 = registerToString(convertRegisterTo8bit(Register::getRegister(free_reg8)));
	const auto free_reg4 = registerToString(convertRegisterTo16bit(Register::getRegister(free_reg8)));
	if(free_reg1.empty()) throw;
	
	if (m_verbose)
	{
		cout << "temporary register needs to be saved: " << boolalpha << save_tmp << endl;
		cout << "temporary register is: " << free_reg8 << endl;
	}

	// stash away original register value (if needed)
	const auto red_zone_reg = string("qword [rsp-128]");
	if (save_tmp)
	{
		s = "mov " + red_zone_reg + ", " + free_reg8;
		traced_instr = insertAssemblyBefore(p_instr, s);
		cout << "save tmp: " << s << endl;
	}

	if (d.getOperand(0)->isRegister())
	{
		auto source_reg = d.getOperand(0)->getString();
		source_reg = registerToString(convertRegisterTo8bit(Register::getRegister(source_reg)));
		s = "movzx " + free_reg4 + ", " + source_reg;
		traced_instr = insertAssemblyBefore(traced_instr, s);
		cout << "movzx val: " << s << endl;
	}
	else
	{
		const auto memopp = d.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		s = "movzx " + free_reg4 + ", byte [" + memop_str + "]"; 
		traced_instr = insertAssemblyBefore(traced_instr, s);
		cout << s << endl;
	}

	// cmp lower_byte(reg), p_immediate
	s = "cmp " + free_reg4 + "," + to_string(lower_byte);
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	if (save_tmp)
	{
		s = "mov " + free_reg8 + ", " + red_zone_reg;
		t = insertAssemblyAfter(t, s);
		cout << "restore tmp: " << s << endl;
	}

	s = "je 0";
	t = insertAssemblyAfter(t, s);
	t->setTarget(traced_instr);
	cout << s << endl;

	s = "jmp 0";
	t = insertAssemblyAfter(t, s);
	t->setTarget(traced_instr);
	cout << s << endl;

	return true;
}

//
// trace <p_num_bytes> before instruction <p_instr> using bytes in <p_constant> starting at memory location <p_mem>
// p_reg is a free register
// p_num_bytes has value 4 or 8
// 
//     t = reg[lodword]           ; t is a register
//     m = k[lodword]             ; m is memory where we stashed the constant
//     cmp t, dword [m]          ; elide if 4 byte compare
//  +- je check_upper            ; elide if 4 byte compare
//  |  cmp t, dword [m]  <--+    ; loop_back
//  |  je orig              |
//  |  t >> 8               |
//  |  m >> 8               |
//  |  jmp -----------------+
//  |
//  check_upper:                ; only if 8 byte compare
//     t1 = reg[hidword]
//     m = k[hidword]
//     cmp t, dword [m]  <--+
//     je orig              |
//     t >> 8               |
//     m >> 8               |
//     jmp -----------------+
//
// orig:
//     cmp reg, K or cmp [], K
//     idiv reg
// 
// note: for div, idiv, need to save/restore flags
//
bool Laf_t::traceBytes48(Instruction_t *p_instr, size_t p_num_bytes, uint64_t p_immediate)
{
	if (p_num_bytes!=2 && p_num_bytes==4 && p_num_bytes==8)
		throw std::domain_error("laf transform only handles 2,4,8 byte compares/div");

	// bail out, there's a reloc here
	if (p_instr->getRelocations().size() > 0)
	{
		m_skip_relocs++;
		return false;
	}

	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;

	//
	// start instrumenting below
	//
	
	// [rsp-128] contains the constant
	// [rsp-128+16] used to save register (if we can't find a free register)
	auto s = string();
	auto t = p_instr;
	auto traced_instr = p_instr;

	// copy constant into bottom of red zone
	const auto mem = string("rsp-128"); // use last 4 or 8 byte of red zone
	if (p_num_bytes == 4)
	{
		s = "mov dword[" + mem + "], " + to_string(p_immediate);
		traced_instr = insertAssemblyBefore(t, s);
		cout << s << endl;
	}
	else				
	{
		s = "mov qword[" + mem + "], " + to_string(p_immediate);
		traced_instr = insertAssemblyBefore(t, s);
		cout << s << endl;
	}

	// copy value to compare into free register
	auto save_tmp = true;
	auto free_reg8 = string("");
	const auto div = d.getMnemonic()== "div" || d.getMnemonic()=="idiv";
	if (div)
		save_tmp = getFreeRegister(p_instr, free_reg8, RegisterSet_t({rn_RBX, rn_RCX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));
	else
		save_tmp = getFreeRegister(p_instr, free_reg8, RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));
	const auto free_reg4 = registerToString(convertRegisterTo32bit(Register::getRegister(free_reg8)));
	if(free_reg8.empty()) throw;
	
	if (m_verbose)
	{
		cout << "temporary register needs to be saved: " << boolalpha << save_tmp << endl;
		cout << "temporary register is: " << free_reg8 << endl;
	}

	// stash away original register value (if needed)
	const auto red_zone_reg = string(" qword [rsp-128+16] ");
	if (save_tmp)
	{
		s = "mov " + red_zone_reg + ", " + free_reg8;
		t = insertAssemblyAfter(t, s);
		cout << "save tmp: " << s << endl;
	}

	if (d.getOperand(0)->isRegister())
	{
		auto source_reg = d.getOperand(0)->getString();
		source_reg = registerToString(convertRegisterTo32bit(Register::getRegister(source_reg)));
		s = "mov " + free_reg4 + ", " + source_reg;
		t = insertAssemblyAfter(t, s);
		cout << "mov val: " << s << endl;
	}
	else
	{
		const auto memopp = d.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		s = "mov " + free_reg4 + ", dword [ " + memop_str + "]"; 
		t = insertAssemblyAfter(t, s);
		cout << s << endl;
	}

	auto patch_check_upper = (Instruction_t*) nullptr;
	if (p_num_bytes == 8)
	{
		s = "cmp " + free_reg4 + ", dword [" + mem + "]";
		t = insertAssemblyAfter(t, s);
		cout << s << endl;

		s = "je 0"; // check_upper
		patch_check_upper = t = insertAssemblyAfter(t, s); // target will need to be set
		cout << s << endl;
	}

	// loop_back
	s = "cmp " + free_reg4 + ", dword [" + mem + "]";
	const auto loop_back = t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "je 0"; // orig
	t = insertAssemblyAfter(t, s);
	t->setTarget(traced_instr);
	cout << s << endl;
	
	s = "shr " + free_reg4 + ", 8";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "shr dword [" + mem + "], 8";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "jmp 0"; // loop_back
	t = insertAssemblyAfter(t, "jmp 0"); // loop_back
	t->setTarget(loop_back);
	t->setFallthrough(nullptr);
	cout << s << endl;

	if (save_tmp)
	{
		s = "mov " + free_reg8 + ", " + red_zone_reg;
		insertAssemblyBefore(traced_instr, s);
		cout << "restore tmp:" << s << endl;
	}

	if (p_num_bytes == 4)
	{
		return true;
	}

	// check_upper
	//
	// mem is memory location representing constant (hidword)
	// p_reg is register representing the value to be checked (hidword)
	//
//  check_upper: 
//     t1 = reg[hidword]
//     m = k[hidword]
//     cmp t, dword [m]  <--+
//     je orig              |
//     t >> 8               |
//     m >> 8               |
//     jmp -----------------+
//

	if (d.getOperand(0)->isRegister())
	{
		s = "mov " + free_reg8 + ", " + d.getOperand(0)->getString();
		t = insertAssemblyAfter(t, s);
		patch_check_upper->setTarget(t);
		cout << "mov val(2): " << s << endl;

		// we want the upper 32-bit of the register
		s = "shr " + free_reg8 + ", 32";
		t = insertAssemblyAfter(t, s);
		cout << s << endl;
	}
	else
	{
		// we want the upper 32 bits of the memory access
		const auto memopp = d.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		s = "mov " + free_reg4 + ", dword [ " + memop_str + "+4]"; 
		t = insertAssemblyAfter(t, s);
		patch_check_upper->setTarget(t);
		cout << s << endl;
	}


	s = "cmp " + free_reg4 + ", dword [" + mem + "+4]"; // loop_back2
	auto loop_back2 = t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "je 0"; // orig
	t = insertAssemblyAfter(t, s);
	t->setTarget(traced_instr);
	cout << s << endl;

	s = "shr " + free_reg4 + ", 8";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "shr dword [" + mem + "+4], 8";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "jmp 0"; // loop_back2
	t = insertAssemblyAfter(t, s);
	t->setTarget(loop_back2);
	cout << s << endl;

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

