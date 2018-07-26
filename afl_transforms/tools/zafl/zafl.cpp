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

#include "zafl.hpp"

#include <stdlib.h>
#include <libIRDB-cfg.hpp>
#include <libElfDep.hpp>
#include <Rewrite_Utility.hpp>
#include <utils.hpp>

using namespace std;
using namespace libTransform;
using namespace libIRDB;
using namespace Zafl;
using namespace IRDBUtility;

Zafl_t::Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_stars_analysis_engine(p_dbinterface),
	m_verbose(p_verbose)
{
        auto ed=ElfDependencies_t(getFileIR());
        (void)ed.appendLibraryDepedencies("libzafl.so");

        m_trace_map = ed.appendGotEntry("zafl_trace_map");
        m_prev_id = ed.appendGotEntry("zafl_prev_id");
}

static void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
        auto r=new Relocation_t(BaseObj_t::NOT_IN_DATABASE, wrt.second, "pcrel", wrt.first);
        fir->GetRelocations().insert(r);
        i->GetRelocations().insert(r);
}

zafl_blockid_t Zafl_t::get_blockid() 
{
	auto counter = 0;
	auto blockid = 0;

	while (counter++ < 100) {
		blockid = rand() % 0xFFFF;
		if (m_used_blockid.find(blockid) == m_used_blockid.end())
		{
			m_used_blockid.insert(blockid);
			return blockid;
		}
	}
	return blockid;
}

/*
        zafl_trace_bits[zafl_prev_id ^ id]++;
        zafl_prev_id = id >> 1;     
*/
void Zafl_t::afl_instrument_bb(Instruction_t *inst)
{
	char buf[8192];
	auto tmp = inst;
	     insertAssemblyBefore(getFileIR(), tmp, "push rax");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "push rcx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "push rdx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pushf"); // this is expensive, optimize away when flags dead

	auto blockid = get_blockid();

/*
   0:   48 8b 15 00 00 00 00    mov    rdx,QWORD PTR [rip+0x0]        # 7 <f+0x7>
   7:   48 8b 0d 00 00 00 00    mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>
   e:   0f b7 02                movzx  eax,WORD PTR [rdx]                      
  11:   66 35 34 12             xor    ax,0x1234                              
  15:   0f b7 c0                movzx  eax,ax                                
  18:   48 03 01                add    rax,QWORD PTR [rcx]                  
  1b:   80 00 01                add    BYTE PTR [rax],0x1                  
  1e:   b8 1a 09 00 00          mov    eax,0x91a                          
  23:   66 89 02                mov    WORD PTR [rdx],ax       
*/	
	static unsigned labelid = 0; 
	labelid++;

   	                         sprintf(buf, "L%d: mov  rdx, QWORD [rel L%d]", labelid, labelid);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	create_got_reloc(getFileIR(), m_prev_id, tmp);

                                 sprintf(buf, "X%d: mov  rcx, QWORD [rel X%d]", labelid, labelid);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	create_got_reloc(getFileIR(), m_trace_map, tmp);

	tmp = insertAssemblyAfter(getFileIR(), tmp, "movzx  eax,WORD [rdx]");

	                               sprintf(buf, "xor    ax,0x%x", blockid);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);

	tmp = insertAssemblyAfter(getFileIR(), tmp, "movzx  eax,ax");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "add    rax,QWORD [rcx]");                  
	tmp = insertAssemblyAfter(getFileIR(), tmp, "add    BYTE [rax],0x1");                  

	                               sprintf(buf, "mov    eax, 0x%x", blockid >> 1);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	tmp = insertAssemblyAfter(getFileIR(), tmp, "mov    WORD [rdx], ax");

	tmp = insertAssemblyAfter(getFileIR(), tmp, "popf");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rdx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rcx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rax");

}

/*
 * Execute the transform.
 *
 * preconditions: the FileIR is read as from the IRDB. valid file listing functions to auto-initialize
 * postcondition: instructions added to auto-initialize stack for each specified function
 *
 */
int Zafl_t::execute()
{
	auto num_bb_instrumented = 0;
	auto num_orphan_instructions = 0;
//	m_stars_analysis_engine.do_STARS(getFileIR());

	// for all functions
	//    for all basic blocks
	//          afl_instrument
	for (auto f : getFileIR()->GetFunctions())
	{
		if (f && f->GetName()[0] == '.')
			continue;
		auto current = num_bb_instrumented;
		ControlFlowGraph_t cfg(f);
		for (auto bb : cfg.GetBlocks())
		{
			afl_instrument_bb(bb->GetInstructions()[0]);
			num_bb_instrumented++;
		}
		
		if (f) {
			cout << "Function " << f->GetName() << " has " << dec << num_bb_instrumented - current << " basic blocks instrumented" << endl;
		}
	}

	for (auto i : getFileIR()->GetInstructions())
	{
		if (i && (i->GetFunction() == NULL))		
			num_orphan_instructions++;
	}

	cout << "#ATTRIBUTE num_bb_instrumented=" << dec << num_bb_instrumented << endl;
	cout << "#ATTRIBUTE num_orphan_instructions=" << dec << num_orphan_instructions << endl;

	return 1;
}
