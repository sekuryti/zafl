/***************************************************************************
 * Copyright (c)  2018  Zephyr Software LLC. All rights reserved.
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

#include <sstream>

#include "constant_decompose.hpp"

#include <Rewrite_Utility.hpp>
#include <utils.hpp>

using namespace std;
using namespace libTransform;
using namespace IRDB_SDK;
using namespace ConstantDecompose;
using namespace IRDBUtility;

ConstantDecompose_t::ConstantDecompose_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_verbose(p_verbose)
{
}


/*
 * Execute the transform.
 *
 * preconditions: the FileIR is read as from the IRDB. valid file listing functions to auto-initialize
 * postcondition: instructions added to auto-initialize stack for each specified function
 *
 */
int ConstantDecompose_t::execute()
{
#ifdef COMMENT
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
#endif

	auto to_decompose = vector<Instruction_t*>(); 
	for(auto func : getFileIR()->getFunctions())
	{

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
				to_decompose.push_back(i);
		};

	};

	// transform each comparison that needs to be decomposed
	for(auto c : to_decompose)
	{
		const auto d_cp = DecodedInstruction_t::factory(c);
		const auto &d_c = *d_cp;

		auto jcc = (Instruction_t*) c->getFallthrough();
		const auto d_cbrp = DecodedInstruction_t::factory(jcc); 
		const auto &d_cbr = *d_cbrp;
		const auto is_jne = (d_cbr.getMnemonic() == "jne");
		const auto is_je = !is_jne;
		auto orig_jcc_fallthrough = jcc->getFallthrough();
		auto orig_jcc_target = jcc->getTarget();

		string s;

// found comparison: 
//                c:  cmp dword [rbp - 4], 0x12345678    or c:  cmp reg, 0x12345678
//                jcc:  jne foobar
		const auto immediate = d_c.getImmediate();
		cout << "found comparison: " << c->getDisassembly() << " immediate: " << hex << immediate << " " << jcc->getDisassembly() << endl;
		if (is_jne)
			cout << "is jump not equal" << endl;
		else
			cout << "is jump equal" << endl;

		auto byte0 = immediate >> 24;	              // high byte
		auto byte1 = (immediate&0xff0000) >> 16;	
		auto byte2 = (immediate&0xff00) >> 8;	
		auto byte3 = immediate&0xff;	              // low byte

		cout <<"    bytes: " << hex << byte0 << byte1 << byte2 << byte3 << endl;

		const auto memopp = d_c.getOperand(0);
		const auto &memop = *memopp;

		auto init_sequence = string();

		// need a free register
		string free_reg = "r15d"; // use STARS dead regs annotation?

		if (d_c.getOperand(0)->isRegister())
		{
			// cmp eax, 0x12345678
			stringstream ss;
			if (d_c.getOperand(0)->getString().find("r15") != string::npos)
			{
				cerr << "Skip instruction: " << c->getDisassembly() << " as r15 is used in cmp" << endl;
				continue;
			}
			else
			{
				ss << "mov " << free_reg << ", " << d_c.getOperand(0)->getString();
			}
			init_sequence = ss.str();
		}
		else
		{
			// cmp dword [rbp - 4], 0x12345678 
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
		getFileIR()->registerAssembly(c, s);
		cout << s << endl;

		s = "sar " + free_reg + ", 0x18";
		t = insertAssemblyAfter(c, s);
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
	};

	return 1;	 // true means success
}
