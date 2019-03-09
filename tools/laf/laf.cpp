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
	m_split_branch = true;

	auto deep_analysis=DeepAnalysis_t::factory(getFileIR());
	leaf_functions = deep_analysis->getLeafFunctions();
	dead_registers = deep_analysis->getDeadRegisters();

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

	m_num_cmp_jcc = 0;
	m_num_cmp_jcc_instrumented = 0;
	m_skip_easy_val = 0;
	m_skip_qword = 0;
	m_skip_relocs = 0;
	m_skip_stack_access = 0;
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

void Laf_t::setSplitBranch(bool p_val)
{
	m_split_branch = p_val;
}

bool Laf_t::getSplitCompare() const
{
	return m_split_compare;
}

bool Laf_t::getSplitBranch() const
{
	return m_split_branch;
}

bool Laf_t::hasLeafAnnotation(Function_t* fn) const
{
	auto it = leaf_functions -> find(fn);
	return (it != leaf_functions->end());
}

int Laf_t::execute()
{
	if (getSplitCompare())
		doSplitCompare();

	cout << "#ATTRIBUTE num_cmp_jcc_patterns=" << dec << m_num_cmp_jcc << endl;
	cout << "#ATTRIBUTE num_cmp_jcc_instrumented=" << dec << m_num_cmp_jcc_instrumented << endl;
	cout << "#ATTRIBUTE num_cmp_jcc_skipped_easyval=" << m_skip_easy_val << endl;
	cout << "#ATTRIBUTE num_cmp_jcc_skipped_qword=" << m_skip_qword << endl;
	cout << "#ATTRIBUTE num_cmp_jcc_skipped_relocs=" << m_skip_relocs << endl;
	cout << "#ATTRIBUTE num_cmp_jcc_skipped_stack_access=" << m_skip_stack_access << endl;
	return 1;
}

// handle comparisons of the form: 
//                c:  cmp [rbp - 4], 0x12345678    or c:  cmp reg, 0x12345678
//                jcc:  jne foobar
bool Laf_t::doSplitCompare(Instruction_t* p_instr, bool p_honor_red_zone)
{
	const auto d_cp = DecodedInstruction_t::factory(p_instr);
	const auto &d_c = *d_cp;

	const auto immediate = d_c.getImmediate();
	auto jcc = p_instr->getFallthrough();
	const auto d_cbrp = DecodedInstruction_t::factory(jcc); 
	const auto &d_cbr = *d_cbrp;
	const auto is_jne = (d_cbr.getMnemonic() == "jne");
	const auto is_je = !is_jne;
	auto orig_jcc_fallthrough = jcc->getFallthrough();
	auto orig_jcc_target = jcc->getTarget();
	auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_RDI, rn_RSI, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});
	auto save_temp = true;

	const auto orig_relocs = p_instr->getRelocations();
	const auto do_relocs = orig_relocs.size() > 0;

	const auto byte0 = immediate >> 24;	              // high byte
	const auto byte1 = (immediate&0xff0000) >> 16;	
	const auto byte2 = (immediate&0xff00) >> 8;	
	const auto byte3 = immediate&0xff;	              // low byte

	cout << "found comparison: " << hex << p_instr->getBaseID() << ": " << p_instr->getDisassembly() << " immediate: " << immediate << " " << jcc->getDisassembly() << endl;

	if (do_relocs)
	{
		cout << " relocs: has reloc size: " << orig_relocs.size() << endl;
		m_skip_relocs++;
		return false;
	}

	if (d_c.getOperand(0)->isRegister())
	{
		const auto r = d_c.getOperand(0)->getString();
		const auto reg = Register::getRegister(r);
		allowed_regs.erase(reg);
	}

	auto free_reg = string(); 
	const auto dead_regs = getDeadRegs(p_instr);
	auto free_regs = getFreeRegs(dead_regs, allowed_regs);

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

	if (free_regs.size() > 0)
	{
		const auto first_free_register = FIRSTOF(free_regs);
		free_regs.erase(first_free_register);
		free_reg = registerToString(convertRegisterTo32bit(first_free_register));
		save_temp = false;
		// free register available --> instrumentation doesn't disturb stack --> no need to deal with red zone
		p_honor_red_zone = false; 
	}
	else
	{
		const auto disasm = p_instr->getDisassembly();
		if (disasm.find("r12")==string::npos)
			free_reg = "r12d";
		else if (disasm.find("r13")==string::npos)
			free_reg = "r13d";
		else if (disasm.find("r14")==string::npos)
			free_reg = "r14d";
		else if (disasm.find("r15")==string::npos)
			free_reg = "r15d";
		else
		{
			cout << "Skip instruction - no free register found: " << disasm << endl;
			return false;
		}
		save_temp = true;
	}

	// handle the case where instrumentation modifies the stack
	// but the cmp instruction uses a stack relative address
	// @todo: adjust the offset in the cmp instruction instead of skipping
	if ((p_honor_red_zone || save_temp) && !d_c.getOperand(0)->isRegister())
	{
		// cmp dword [rbp - 4], 0x12345678 
		const auto memopp = d_c.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		if (memop_str.find("rbp")!=string::npos ||
		    memop_str.find("rsp")!=string::npos ||
		    memop_str.find("ebp")!=string::npos ||
		    memop_str.find("esp")!=string::npos)
		{
			cout << "comparison accesses the stack -- skip" << endl;
			m_skip_stack_access++;
			return false;
		}
	}
	
	const auto get_reg64 = [](string r) -> string { 
			return registerToString(convertRegisterTo64bit(Register::getRegister(r)));
		};

	const auto push_reg = "push " + get_reg64(free_reg);
	const auto pop_reg = "pop " + get_reg64(free_reg);
	const auto push_redzone = string("lea rsp, [rsp-128]");
	const auto pop_redzone = string("lea rsp, [rsp+128]");
	
	//
	// Start changing the IR
	//
	
	string s;

	auto init_sequence = string();

	if (d_c.getOperand(0)->isRegister())
	{
		// cmp eax, 0x12345678
		stringstream ss;
		ss << "mov " << free_reg << ", " << d_c.getOperand(0)->getString();
		init_sequence = ss.str();
	}
	else
	{
		// cmp dword [rbp - 4], 0x12345678 
		const auto memopp = d_c.getOperand(0);
		const auto &memop = *memopp;
		const auto memop_str = memop.getString();
		init_sequence = "mov " + free_reg + ", dword [ " + memop_str + " ]";
	}

	cout << "decompose sequence: free register: " << free_reg << endl;
	cout << "init sequence is: " << init_sequence << endl;

/*
	Handle high-order byte:
		mov eax, dword [rbp - 4]
		sar eax, 0x18
		cmp eax, 0x12
		jne j
*/
	stringstream ss;
	s = init_sequence;

	if (p_honor_red_zone)
		p_instr = insertAssemblyBefore(p_instr, push_redzone);

	if (save_temp)
		p_instr = insertAssemblyBefore(p_instr, push_reg);

	// overwrite the original cmp instruction
	getFileIR()->registerAssembly(p_instr, s); 
	cout << s << endl;

	s = "sar " + free_reg + ", 0x18";
	auto t = insertAssemblyAfter(p_instr, s);
	cout << s << endl;

	ss.str("");
	ss << "cmp " << free_reg << ", 0x" << hex << byte0;
	s = ss.str();
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	if (save_temp)
	{
		t = insertAssemblyAfter(t, pop_reg);
		cout << pop_reg << endl;
	}

	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, "lea rsp, [rsp+128]");

	s = "jne 0";
	t = insertAssemblyAfter(t, s);
	if (is_je) {
		t->setTarget(orig_jcc_fallthrough);
		cout << "target: original fallthrough ";
	}
	else {
		t->setTarget(orig_jcc_target);
		cout << "target: original target ";
	}

	cout << s << endl;

/*
	Handle 2nd high-order byte:
		mov eax, dword [rbp - 4]
		and eax, 0xff0000
		sar eax, 0x10
		cmp eax, 0x34
		jne xxx
*/
	s = init_sequence;

	if (p_honor_red_zone)
	{
		t = insertAssemblyAfter(t, push_redzone);
		cout << push_redzone << endl;
	}

	if (save_temp)
	{
		t = insertAssemblyAfter(t, push_reg);
		cout << push_reg << endl;
	}

	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "and " + free_reg + ", 0xff0000";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "sar " + free_reg + ", 0x10";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	ss.str("");
	ss << "cmp " << free_reg << ", 0x" << hex << byte1;
	s = ss.str();
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	if (save_temp)
	{
		t = insertAssemblyAfter(t, pop_reg);
		cout << pop_reg << endl;
	}

	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, pop_redzone);

	s = "jne 0";
	t = insertAssemblyAfter(t, s);
	if (is_je) {
		t->setTarget(orig_jcc_fallthrough);
		cout << "target: original fallthrough ";
	}
	else {
		t->setTarget(orig_jcc_target);
		cout << "target: original target ";
	}
	cout << s << endl;

/*
	Handle 3rd high-order byte:
		mov    eax,DWORD PTR [rbp-0x4]
		and    eax,0xff00
		sar    eax,0x8
		cmp    eax,0x56
		jne    xxx
*/
	s = init_sequence;
	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, push_redzone);

	if (save_temp)
	{
		t = insertAssemblyAfter(t, push_reg);
		cout << push_reg << endl;
	}
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "and " + free_reg + ", 0xff00";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "sar " + free_reg + ", 0x8";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	ss.str("");
	ss << "cmp " << free_reg << ", 0x" << hex << byte2;
	s = ss.str();
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	if (save_temp)
	{
		t = insertAssemblyAfter(t, pop_reg);
		cout << pop_reg << endl;
	}

	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, pop_redzone);

	s = "jne 0";
	t = insertAssemblyAfter(t, s);
	if (is_je) {
		t->setTarget(orig_jcc_fallthrough);
		cout << "target: original fallthrough ";
	}
	else {
		t->setTarget(orig_jcc_target);
		cout << "target: original target ";
	}
	cout << s << endl;

/*
	Handle low-order byte:
            mov      eax,DWORD PTR [rbp-0x4]
            and      eax,0xff
            cmp      eax,0x78
            je,jne   xxx
*/
	s = init_sequence;
	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, push_redzone);

	if (save_temp)
	{
		t = insertAssemblyAfter(t, push_reg);
		cout << push_reg << endl;
	}
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	s = "and " + free_reg + ", 0xff";
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	ss.str("");
	ss << "cmp " << free_reg << ", 0x" << hex << byte3;
	s = ss.str();
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

	if (save_temp)
		t = insertAssemblyAfter(t, pop_reg);

	if (p_honor_red_zone)
		t = insertAssemblyAfter(t, pop_redzone);

	s = is_je ? "je 0" : "jne 0";
	t = insertAssemblyAfter(t, s);
	t->setTarget(orig_jcc_target);
	t->setFallthrough(orig_jcc_fallthrough);
	cout << s << " ";
	cout << "target: original target   fallthrough: original fallthrough " << endl;

	return true;
}

int Laf_t::doSplitCompare()
{
/*
	// look for cmp against constant
0000000000400526 <main>:
  400535:	81 7d fc 78 56 34 12 	cmp    DWORD PTR [rbp-0x4],0x12345678
  40053c:	75 0a                	jne    400548 <main+0x22>
  40054e:	c3                   	ret    

	// decompose into series of 1-byte comparisons
  4005b9:	8b 45 fc             	mov    eax,DWORD PTR [rbp-0x4]
  4005bc:	c1 f8 18             	sar    eax,0x18
  4005bf:	83 f8 12             	cmp    eax,0x12
  4005c2:	75 32                	jne    4005f6 <main+0x80>
  4005c4:	8b 45 fc             	mov    eax,DWORD PTR [rbp-0x4]
  4005c7:	25 00 00 ff 00       	and    eax,0xff0000
  4005cc:	c1 f8 10             	sar    eax,0x10
  4005cf:	83 f8 34             	cmp    eax,0x34
  4005d2:	75 22                	jne    4005f6 <main+0x80>
  4005d4:	8b 45 fc             	mov    eax,DWORD PTR [rbp-0x4]
  4005d7:	25 00 ff 00 00       	and    eax,0xff00
  4005dc:	c1 f8 08             	sar    eax,0x8
  4005df:	83 f8 56             	cmp    eax,0x56
  4005e2:	75 12                	jne    4005f6 <main+0x80>
  4005e4:	8b 45 fc             	mov    eax,DWORD PTR [rbp-0x4]
  4005e7:	0f b6 c0             	movzx  eax,al
  4005ea:	83 f8 78             	cmp    eax,0x78
  4005ed:	75 07                	jne    4005f6 <main+0x80>
  4005ef:	b8 01 00 00 00       	mov    eax,0x1
  4005f4:	eb 05                	jmp    4005fb <main+0x85>
  4005f6:	b8 00 00 00 00       	mov    eax,0x0
  4005fb:	c9                   	leave  
  4005fc:	c3                   	ret    
*/

	auto to_split_compare = vector<Instruction_t*>(); 
	for(auto func : getFileIR()->getFunctions())
	{
		if (isBlacklisted(func))
			continue;

		 for(auto i : func->getInstructions())
		 {
			const auto dp = DecodedInstruction_t::factory(i);
			const auto &d = *dp;
			if (d.getMnemonic()!="cmp") continue;
			if (d.getOperands().size()!=2) continue;

			const auto fp = DecodedInstruction_t::factory(i->getFallthrough());
			const auto &f = *fp;
			if (f.getMnemonic() != "je" && f.getMnemonic() !="jeq" && f.getMnemonic() !="jne") continue;

			m_num_cmp_jcc++;

			if (!d.getOperand(1)->isConstant()) continue;
			if (d.getOperand(0)->getArgumentSizeInBytes()==8)
				m_skip_qword++;
			if (d.getOperand(0)->getArgumentSizeInBytes()!=4) continue;
			if (!i->getFallthrough()) continue;

			/* these values are easy for fuzzers to guess
			const auto imm = d.getImmediate();
			if (imm == 0 || imm == 1 || imm == -1)
			{
				m_skip_easy_val++;
				continue;
			}
			*/

			// we now have a cmp instruction to split
			if (d.getOperand(0)->isRegister() || d.getOperand(0)->isMemory())
				to_split_compare.push_back(i);
		};

	};

	// split comparisons
	for(auto c : to_split_compare)
	{
		const auto honorRedZone = hasLeafAnnotation(c->getFunction());
		if (doSplitCompare(c, honorRedZone))
			m_num_cmp_jcc_instrumented++;
	}

	return 1;	 // true means success
}
