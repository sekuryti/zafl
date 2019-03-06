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

int Laf_t::execute()
{
	if (getSplitCompare())
		doSplitCompare();

	return 1;
}

// handle comparisons of the form: 
//                c:  cmp [rbp - 4], 0x12345678    or c:  cmp reg, 0x12345678
//                jcc:  jne foobar
void Laf_t::doSplitCompare(Instruction_t* p_instr)
{
	const auto d_cp = DecodedInstruction_t::factory(p_instr);
	const auto &d_c = *d_cp;

	auto jcc = p_instr->getFallthrough();
	const auto d_cbrp = DecodedInstruction_t::factory(jcc); 
	const auto &d_cbr = *d_cbrp;
	const auto is_jne = (d_cbr.getMnemonic() == "jne");
	const auto is_je = !is_jne;
	auto orig_jcc_fallthrough = jcc->getFallthrough();
	auto orig_jcc_target = jcc->getTarget();
	auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});

	const auto dead_regs = getDeadRegs(p_instr);

	if (d_c.getOperand(0)->isRegister())
	{
		const auto r = d_c.getOperand(0)->getString();
		const auto reg = Register::getRegister(r);
		allowed_regs.erase(reg);
	}

//	auto save_temp = true;
	auto free_regs = getFreeRegs(dead_regs, allowed_regs);
	auto free_reg = string(); 

	if (free_regs.size() > 0)
	{
//		save_temp = false;
		const auto first_free_register = FIRSTOF(free_regs);
		free_regs.erase(first_free_register);
		free_reg = registerToString(convertRegisterTo32bit(first_free_register));
	}
	
	// for now, skip if no free register
	if (free_regs.size() == 0)
	{
		cout << "no free register, skipping: " << p_instr->getBaseID() << ": " << p_instr->getDisassembly() << endl;
		return;
	}

	string s;

	const auto immediate = d_c.getImmediate();
	cout << "found comparison: " << p_instr->getDisassembly() << " immediate: " << hex << immediate << " " << jcc->getDisassembly() << endl;
	if (is_jne)
		cout << "is jump not equal" << endl;
	else
		cout << "is jump equal" << endl;

	auto byte0 = immediate >> 24;	              // high byte
	auto byte1 = (immediate&0xff0000) >> 16;	
	auto byte2 = (immediate&0xff00) >> 8;	
	auto byte3 = immediate&0xff;	              // low byte

	if (byte0 == 0x0 && byte1 == 0x0 && byte2 == 0x0)
	{
		cout << "skip as immediate is only 1 byte: 0x" << immediate << endl;
		return;
	}

	cout <<"    bytes: 0x" << hex << byte0 << byte1 << byte2 << byte3 << endl;


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
		init_sequence = "mov " + free_reg + ", dword [ " + memop.getString() + " ]";
	}

	// @todo: save/restore free register r15d 
	cout << "decompose sequence: assume free register: " << free_reg << endl;
	cout << "init sequence is: " << init_sequence << endl;


/*
		mov eax, dword [rbp - 4]
		sar eax, 0x18
		cmp eax, 0x12
		jne j
*/
	stringstream ss;
	auto t = (Instruction_t*) NULL;

	s = init_sequence;
	getFileIR()->registerAssembly(p_instr, s);
	cout << s << endl;

	s = "sar " + free_reg + ", 0x18";
	t = insertAssemblyAfter(p_instr, s);
	cout << s << endl;

	ss.str("");
	ss << "cmp " << free_reg << ", 0x" << hex << byte0;
	s = ss.str();
	t = insertAssemblyAfter(t, s);
	cout << s << endl;

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
		mov eax, dword [rbp - 4]
		and eax, 0xff0000
		sar eax, 0x10
		cmp eax, 0x34
		jne xxx
*/
	s = init_sequence;
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
		mov    eax,DWORD PTR [rbp-0x4]
		and    eax,0xff00
		sar    eax,0x8
		cmp    eax,0x56
		jne    xxx
*/
	s = init_sequence;
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
  4005e4:	8b 45 fc             	mov    eax,DWORD PTR [rbp-0x4]
  4005e7:	0f b6 c0             	movzx  eax,al
  4005ea:	83 f8 78             	cmp    eax,0x78
  4005ed:	75 07                	jne    4005f6 <main+0x80>
*/
	s = init_sequence;
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

	if (is_je)
		s = "je 0";
	else
		s = "jne 0";
	t = insertAssemblyAfter(t, s);
	t->setTarget(orig_jcc_target);
	t->setFallthrough(orig_jcc_fallthrough);
	cout << "target: original target   fallthrough: original fallthrough ";
	cout << s << endl;
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
			if (!d.getOperand(1)->isConstant()) continue;
			if (d.getOperand(0)->getArgumentSizeInBytes()!=4) continue;
			if (!i->getFallthrough()) continue;
			const auto fp = DecodedInstruction_t::factory(i->getFallthrough());
			const auto &f = *fp;
			if (f.getMnemonic() != "je" && f.getMnemonic() !="jeq" && f.getMnemonic() !="jne") continue;
			
			const auto imm = d.getImmediate();
			if (imm == 0 || imm == 1 || imm == -1 || imm == 0xff || imm == 0xffff)
				continue;

			// we now have a cmp instruction to decompose
			if (d.getOperand(0)->isRegister() || d.getOperand(0)->isMemory())
				to_split_compare.push_back(i);
		};

	};

	// transform each comparison that needs to be decomposed
	for(auto c : to_split_compare)
	{
		doSplitCompare(c);
	}

	return 1;	 // true means success
}
