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
	Transform(p_variantIR),
	m_dbinterface(p_dbinterface),
	m_verbose(p_verbose)
{
	m_split_compare = true;
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
	return (p_func->getName()[0] == '.' || 
	        p_func->getName().find("@plt") != string::npos ||
	        p_func->getName().find("__libc_") != string::npos ||
	        m_blacklist.find(p_func->getName())!=m_blacklist.end());
}

void Laf_t::setSplitCompare(bool p_val)
{
	m_split_compare = p_val;
}

bool Laf_t::getSplitCompare() const
{
	return m_split_compare;
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


/*
 *  p_instr is the cmp instruction to instrument
 */
Instruction_t* Laf_t::traceDword(Instruction_t* p_instr, const size_t p_num_bytes, const vector<string> p_init_sequence, const uint32_t p_immediate, const string p_freereg)
{
	assert(p_num_bytes > 0 && p_num_bytes <= 4);
	assert(!p_init_sequence.empty());
	assert(!p_freereg.empty());

	/*
               mov    eax,DWORD PTR [rbp-0x4]
               and    eax,0xff0000
               sar    eax,0x10
               cmp    eax,0x34
               je     next
               nop               
        next:  ...
	*/
	const uint32_t immediate[] = { 
		p_immediate&0xff,                // low byte
		(p_immediate&0xff00) >> 8,	
		(p_immediate&0xff0000) >> 16,	
		p_immediate >> 24 };             // high byte

	const uint32_t mask[] = { 
		0x000000ff, 
		0x0000ff00,
		0x00ff0000,
		0xff000000 };

	const uint32_t sar[] = { 
		0x0, 
		0x8,
		0x10,
		0x18 };


	// p_instr == cmp r13d, 0x1     mov rdi, r13d  <- t
	//                              cmp r13d, 0x1  <- orig
	//
	auto orig_cmp = (Instruction_t*) nullptr;
	for (size_t i = 0; i < p_num_bytes; ++i)
	{
		auto t = addInitSequence(p_instr, p_init_sequence);
		auto orig = t->getFallthrough();
		if (!orig_cmp) orig_cmp = orig;
		// mov eax, dword []     <-- p_instr, t
		// cmp eax, 0x12345678   <-- orig  

		stringstream ss;
		ss.str("");
		ss << "and " << p_freereg << ", 0x" << hex << mask[i];
		auto s=ss.str();
		t = insertAssemblyAfter(t, s);
		if (m_verbose) cout << s << endl;
		// mov eax, dword []     <-- p_instr
		// and eax, 0xmask       <-- t
		// cmp eax, 0x12345678   <-- orig  

		if (sar[i] != 0)
		{
			ss.str("");
			ss << "shr " << p_freereg << ", 0x" << hex << sar[i];
			s=ss.str();
			t = insertAssemblyAfter(t, s);
			if (m_verbose) cout << s << endl;
		}
		// mov eax, dword []     <-- p_instr
		// and eax, 0xmask 
		// sar eax, 0x...        <-- t
		// cmp eax, 0x12345678   <-- orig  

		ss.str("");
		ss << "cmp " << p_freereg << ", 0x" << hex << immediate[i];
		s = ss.str();
		t = insertAssemblyAfter(t, s);
		if (m_verbose) cout << s << endl;
		// mov eax, dword []     <-- p_instr
		// and eax, 0xmask 
		// sar eax, 0x...        
		// cmp eax, 0x...        <-- t
		// cmp eax, 0x12345678   <-- orig  

		s="je 0";
		t = insertAssemblyAfter(t, s);
		t->setTarget(orig);
		if (m_verbose) cout << s << endl;
		// mov eax, dword []     <-- p_instr
		// and eax, 0xmask 
		// sar eax, 0x...        
		// cmp eax, 0x...        
		// je 0                  <-- t
		// cmp eax, 0x12345678   <-- orig  

		s="nop";
		t = insertAssemblyAfter(t, s);
		t->setFallthrough(orig);
		if (m_verbose) cout << s << endl;
		// mov eax, dword []     <-- p_instr
		// and eax, 0xmask       
		// sar eax, 0x...        
		// cmp eax, 0x...        
		// je
		// nop                   <-- t
		// cmp eax, 0x12345678   <-- orig  
	}
	return orig_cmp;
}

bool Laf_t::getFreeRegister(Instruction_t* p_instr, string& p_freereg, RegisterSet_t p_allowed_regs)
{
	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;
	auto save_temp = true;

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
		p_freereg = registerToString(convertRegisterTo32bit(first_free_register));
		save_temp = false;
	}
	else
	{
		const auto disasm = p_instr->getDisassembly();
		if (disasm.find("r11")==string::npos)
			p_freereg = "r11d";
		else if (disasm.find("r12")==string::npos)
			p_freereg = "r12d";
		else if (disasm.find("r13")==string::npos)
			p_freereg = "r13d";
		else if (disasm.find("r14")==string::npos)
			p_freereg = "r14d";
		else if (disasm.find("r15")==string::npos)
			p_freereg = "r15d";
		save_temp = true;
	}

	return save_temp;
}


Instruction_t* Laf_t::addInitSequence(Instruction_t* p_instr, const vector<string> p_sequence)
{
	assert(p_sequence.size() > 0);

	//   p_instr -> <orig>     s1       <-- t      s1         
	//                         <orig>*  <--        s2   <-- t        
	//                                             <orig>*
	auto t = p_instr;
	insertAssemblyBefore(p_instr, p_sequence[0]);
	if (m_verbose) cout << "inserting: " << p_sequence[0] << endl;

	for (auto i = 1u; i < p_sequence.size(); i++)
	{
		t = insertAssemblyAfter(t, p_sequence[i]);
		if (m_verbose) cout << "inserting: " << p_sequence[i] << endl;
	}
	return t;
}

vector<string> Laf_t::getInitSequence(Instruction_t *p_instr, string p_free_reg)
{
	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;
	
	auto init_sequence = vector<string>();

	if (d.getOperand(0)->getArgumentSizeInBytes() == 1)
		throw std::domain_error("operand has unhandled size");
	
	if (d.getOperand(0)->isRegister())
	{
		// cmp eax, 0x12345678
		stringstream ss;
		auto source_reg = d.getOperand(0)->getString();
		switch(d.getOperand(0)->getArgumentSizeInBytes())
		{
			case 2:
				p_free_reg = registerToString(convertRegisterTo16bit(Register::getRegister(p_free_reg)));
				break;
			case 4: // do nothing, p_free_reg is already 32-bit
				break;
			case 8:
				source_reg = registerToString(convertRegisterTo32bit(Register::getRegister(source_reg)));
				break;
			case 1:
			default:
				throw std::domain_error("operand has unhandled size");
		}

		ss << "mov " << p_free_reg << ", " << source_reg;
		init_sequence.push_back(ss.str());
		// mov p_free_reg, eax
	}
	else
	{
		// cmp dword [rbp - 4], 0x12345678 
		const auto memopp = d.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		if (d.getOperand(0)->getArgumentSizeInBytes()==2)
		{
			p_free_reg = registerToString(convertRegisterTo16bit(Register::getRegister(p_free_reg)));
			init_sequence.push_back("mov " + p_free_reg + ", word [ " + memop_str + " ]");
		}
		else
			init_sequence.push_back("mov " + p_free_reg + ", dword [ " + memop_str + " ]");
		// mov p_free_reg, dword [rbp - 4]
	}

	return init_sequence;
}

/*
 *  p_instr: cmp instruction to instrument
 *
 *  Decompose cmp into 4 individual byte comparison blocks.
 *  Note that the 4 blocks do not really do anything.
 *  They will be used to guide AFL.
 *
 *  The original cmp followed by a condition branch will be executed
 *  after our instrumentation.
 *
 *  Note: @todo: we need to make sure that we don't optimize away AFL instrumentation
 *               in subsequent passes
 *
 *  Example:
 *      cmp reg, 0x12345678
 *      je  target
 *
 *  Post instrumentation (assuming eax is a free register):
 *      mov eax, ebx
 *      and eax, 0xff000000
 *      sar eax, 0x18
 *      cmp eax, 0x12
 *		je L1
 *		nop
 *  L1: 
 *      mov eax, ebx
 *      and eax, 0x00ff0000
 *      sar eax, 0x10
 *      cmp eax, 0x34
 *		je L2
 *		nop
 *  L2: 
 *      mov eax, ebx
 *      and eax, 0x0000ff00
 *      sar eax, 0x08
 *      cmp eax, 0x56
 *		je L3
 *		nop
 *  L3: 
 *      mov eax, ebx
 *      and eax, 0x000000ff
 *      cmp eax, 0x78
 *		je L4
 *		nop
 *
 *  L4:
 *      cmp ebx, 0x12345678
 *      je  target
 *
 */
bool Laf_t::instrumentCompare(Instruction_t* p_instr, bool p_honor_red_zone)
{
	// either 4-byte or 8-byte compare
	/* 
		cmp    DWORD PTR [rbp-0x4],0x12345678
		cmp    QWORD PTR [...],0x12345678
		cmp    reg, 0x12345678
		jne,je,jle,jge,jae,jbe
	*/
	
	// bail out, there's a reloc here
	if (p_instr->getRelocations().size() > 0)
	{
		m_skip_relocs++;
		return false;
	}

	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;

	// get a temporary register
	auto free_reg = string("");
	auto save_temp = getFreeRegister(p_instr, free_reg, RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));

	assert(!free_reg.empty());
	
	if (!save_temp)
		p_honor_red_zone = false;
	
	if (m_verbose)
	{
		cout << "temporary register needs to be saved: " << boolalpha << save_temp << endl;
		cout << "temporary register is: " << free_reg << endl;
		cout << "honor red zone: " << boolalpha << p_honor_red_zone << endl;
	}
	
	// if we disturb the stack b/c of saving a register or b/c of the red zone
	// we need to make sure the instruction doesn't also address the stack, e.g. mov [rbp-8], 0
	if ((p_honor_red_zone || save_temp) && memoryStackAccess(p_instr))
	{
		cout << "instrumentation disturbs the stack and original instruction accesses the stack -- skip" << endl;
		m_skip_stack_access++;
		return false;
	}

	// utility strings to save temp register and handle the red zone
	const auto get_reg64 = [](string r) -> string { 
			return registerToString(convertRegisterTo64bit(Register::getRegister(r)));
		};
	const auto push_reg = "push " + get_reg64(free_reg);
	const auto pop_reg = "pop " + get_reg64(free_reg);
	const auto push_redzone = string("lea rsp, [rsp-128]");
	const auto pop_redzone = string("lea rsp, [rsp+128]");
	
	// setup init sequence...
	auto init_sequence = getInitSequence(p_instr, free_reg);
	if (m_verbose)
		cout << "init sequence is: " << init_sequence[0] << endl;

	if (p_honor_red_zone) 
	{
		p_instr = insertAssemblyBefore(p_instr, push_redzone);
		if (m_verbose) cout << push_redzone << endl;
	}
	if (save_temp)
	{
		p_instr = insertAssemblyBefore(p_instr, push_reg);
		if (m_verbose) cout << push_reg << endl;
	}

	// instrument before the compare (p_instr --> "cmp")
	const auto num_bytes_to_compare = (d.getOperand(0)->getArgumentSizeInBytes() == 2) ? 2 : 4; // max is 4
	uint32_t K = d.getImmediate();
	if (m_verbose)
		cout << "handle 4 byte compare against 0x" << hex << K << endl;

	auto cmp = traceDword(p_instr, num_bytes_to_compare, init_sequence, K, free_reg);
	// post: cmp is the "cmp" instruction

	// handle quad word (8 byte compares)
	// by comparing the upper 32-bit against 0 or 0xffffffff depending
	// on whether the constant K is positive or negative
	if (d.getOperand(0)->getArgumentSizeInBytes() == 8)
	{
		const uint32_t imm = (d.getImmediate() >= 0) ? 0 : 0xffffffff;

		if (d.getOperand(0)->isRegister())
		{
			auto init_sequence2 = vector<string>();
			const auto source_reg = d.getOperand(0)->getString();
			const auto free_reg64 = get_reg64(free_reg);
			init_sequence2.push_back("mov " + free_reg64 + ", " + source_reg);
			init_sequence2.push_back("shr " + free_reg64 + ", 0x20"); 
			if (m_verbose)
			{
				cout << "upper init sequence: " << init_sequence2[0] << endl;
				cout << "upper init sequence: " << init_sequence2[1] << endl;
			}

			// now instrument as if immediate=0 or 0xffffffff
			cmp = traceDword(cmp, 4, init_sequence2, imm, free_reg);
		}
		else
		{
			auto init_sequence2 = vector<string>();
			const auto memopp = d.getOperand(0);
			const auto &memop = *memopp;
			const auto memop_str = memop.getString();
			// add 4 bytes to grab upper 32 bits
			init_sequence2.push_back("mov " + free_reg + ", dword [ " + memop_str + " + 4 ]"); 

			if (m_verbose)
				cout << "upper init sequence: " << init_sequence2[0] << endl;

			// now instrument as if immediate=0 or 0xffffffff
			cmp = traceDword(cmp, 4, init_sequence2, imm, free_reg);
		}
	}

	auto t = cmp;

	if (save_temp)
	{
		t = insertAssemblyBefore(t, pop_reg);
		if (m_verbose) cout << pop_reg << endl;
	}
	
	if (p_honor_red_zone)
	{
		t = insertAssemblyBefore(t, pop_redzone);
		if (m_verbose) cout << pop_redzone << endl;
	}

	return true;
}

int Laf_t::doSplitCompare()
{
	for(auto func : getFileIR()->getFunctions())
	{
		auto to_split_compare = vector<Instruction_t*>(); 
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
			/*
			if (!i->getFallthrough()) continue;

			const auto fp = DecodedInstruction_t::factory(i->getFallthrough());
			const auto &f = *fp;
			const auto jmps = set<string>({"je", "jne", "jb", "jnae", "jnb", "jae", "jbe", "jna", "ja", "jnbe", "jl", "jnge", "jge", "jnl", "jle", "jng", "jg", "jnle", "jg", "jnle"});
			if (jmps.find(f.getMnemonic())==jmps.end())
				continue;
			*/

			if (!d.getOperand(1)->isConstant()) continue;

			if (d.getOperand(0)->getArgumentSizeInBytes()==1)
			{
				m_skip_byte++;
				continue;
		 	}

			// we have a cmp followed by a conditial jmp (je, jne)
			m_num_cmp++;

			// we now have a cmp instruction to trace
			// we handle 2, 4, 8 byte compares
			if (d.getOperand(0)->isRegister() || d.getOperand(0)->isMemory())
				to_split_compare.push_back(i);
			else
				m_skip_unknown++;
		};

		// split comparisons
		for(auto c : to_split_compare)
		{
			if (getenv("LAF_LIMIT_END"))
			{
				auto debug_limit_end = static_cast<unsigned>(atoi(getenv("LAF_LIMIT_END")));
				if (m_num_cmp_instrumented >= debug_limit_end)
					break;
			}
			const auto s = c->getDisassembly();
			const auto honorRedZone = hasLeafAnnotation(c->getFunction());
			if (instrumentCompare(c, honorRedZone))
			{
				if (m_verbose) cout << "success for " << s << endl;
				m_num_cmp_instrumented++;
				
		/*
 		getFileIR()->assembleRegistry();
		getFileIR()->setBaseIDS();
		cout << "Post transformation CFG for " << func->getName() << ":" << endl;
		auto post_cfg=ControlFlowGraph_t::factory(func);	
		cout << *post_cfg << endl;
		*/
		
			}
		}

	};

	return 1;	 // true means success
}

bool Laf_t::memoryStackAccess(Instruction_t* p_instr, unsigned p_operandNumber)
{
	const auto dp = DecodedInstruction_t::factory(p_instr);
	const auto &d = *dp;

	if (d.getOperand(p_operandNumber)->isRegister())
		return false;

	const auto memopp = d.getOperand(p_operandNumber);
	const auto &memop = *memopp;
	const auto memop_str = memop.getString();
	return (memop_str.find("rbp")!=string::npos ||
	    memop_str.find("rsp")!=string::npos ||
	    memop_str.find("ebp")!=string::npos ||
	    memop_str.find("esp")!=string::npos);
}

bool Laf_t::instrumentDiv(Instruction_t* p_div, bool p_honor_red_zone)
{
	// bail out, there's a reloc here
	if (p_div->getRelocations().size() > 0)
	{
		m_skip_relocs++;
		return false;
	}

	const auto dp = DecodedInstruction_t::factory(p_div);
	const auto &d = *dp;

	// get a temporary register
	auto free_reg = string("");
	// leave out rax, rdx 
	auto save_temp = getFreeRegister(p_div, free_reg, RegisterSet_t({rn_RBX, rn_RCX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15}));

	assert(!free_reg.empty());
	
	if (!save_temp)
		p_honor_red_zone = false;
	
	if (m_verbose)
	{
		cout << "temporary register needs to be saved: " << boolalpha << save_temp << endl;
		cout << "temporary register is: " << free_reg << endl;
		cout << "honor red zone: " << boolalpha << p_honor_red_zone << endl;
	}
	
	// if we disturb the stack b/c of saving a register or b/c of the red zone
	// we need to make sure the instruction doesn't also address the stack, e.g. mov [rbp-8], 0
	if ((p_honor_red_zone || save_temp) && memoryStackAccess(p_div))
	{
		cout << "instrumentation disturbs the stack and original instruction accesses the stack -- skip" << endl;
		m_skip_stack_access++;
		return false;
	}

	// utility strings to save temp register and handle the red zone
	const auto get_reg64 = [](string r) -> string { 
			return registerToString(convertRegisterTo64bit(Register::getRegister(r)));
		};
	const auto push_reg = "push " + get_reg64(free_reg);
	const auto pop_reg = "pop " + get_reg64(free_reg);
	const auto push_redzone = string("lea rsp, [rsp-128]");
	const auto pop_redzone = string("lea rsp, [rsp+128]");
	
	// setup init sequence...
	auto init_sequence = getInitSequence(p_div, free_reg);
	if (m_verbose)
		cout << "init sequence is: " << init_sequence[0] << endl;
	
	if (p_honor_red_zone) 
	{
		p_div = insertAssemblyBefore(p_div, push_redzone);
		if (m_verbose) cout << push_redzone << endl;
	}
	if (save_temp)
	{
		p_div = insertAssemblyBefore(p_div, push_reg);
		if (m_verbose) cout << push_reg << endl;
	}

	// guide fuzzer by inducing comparison against 0
	const auto num_bytes_to_compare = (d.getOperand(0)->getArgumentSizeInBytes() == 2) ? 2 : 4; // max is 4
	auto t = traceDword(p_div, num_bytes_to_compare, init_sequence, 0, free_reg); 

	if (d.getOperand(0)->getArgumentSizeInBytes() == 8)
	{
		if (d.getOperand(0)->isRegister())
		{
			auto init_sequence2 = vector<string>();
			const auto source_reg = d.getOperand(0)->getString();
			const auto free_reg64 = get_reg64(free_reg);
			init_sequence2.push_back("mov " + free_reg64 + ", " + source_reg);
			init_sequence2.push_back("shr " + free_reg64 + ", 0x20"); 
			if (m_verbose)
			{
				cout << "upper init sequence: " << init_sequence2[0] << endl;
				cout << "upper init sequence: " << init_sequence2[1] << endl;
			}

			t = traceDword(t, 4, init_sequence2, 0, free_reg);
		}
		else
		{
			auto init_sequence2 = vector<string>();
			const auto memopp = d.getOperand(0);
			const auto &memop = *memopp;
			const auto memop_str = memop.getString();
			// add 4 bytes to grab upper 32 bits
			init_sequence2.push_back("mov " + free_reg + ", dword [ " + memop_str + " + 4 ]"); 
			if (m_verbose)
				cout << "upper init sequence: " << init_sequence2[0] << endl;
			t = traceDword(t, 4, init_sequence2, 0, free_reg);
		}
	}

	if (save_temp)
	{
		t = insertAssemblyBefore(t, pop_reg);
		if (m_verbose) cout << pop_reg << endl;
	}
	
	if (p_honor_red_zone)
	{
		t = insertAssemblyBefore(t, pop_redzone);
		if (m_verbose) cout << pop_redzone << endl;
	}

	return true;
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
			if (d.getOperand(0)->getArgumentSizeInBytes()<2) continue;

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
			const auto honorRedZone = hasLeafAnnotation(c->getFunction());
			if (instrumentDiv(c, honorRedZone))
			{
				if (m_verbose)
					cout << "success for " << s << endl;
				m_num_div_instrumented++;
				
		/*
 		getFileIR()->assembleRegistry();
		getFileIR()->setBaseIDS();
		cout << "Post transformation CFG for " << func->getName() << ":" << endl;
		auto post_cfg=ControlFlowGraph_t::factory(func);	
		cout << *post_cfg << endl;
		*/
		
			}
		}

	};

	return 1;	 // true means success
}

int Laf_t::execute()
{
	if (getSplitCompare())
		doSplitCompare();

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

