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
#include <MEDS_DeadRegAnnotation.hpp>
#include <MEDS_SafeFuncAnnotation.hpp>
#include <utils.hpp> 

using namespace std;
using namespace libTransform;
using namespace libIRDB;
using namespace Zafl;
using namespace IRDBUtility;
using namespace MEDS_Annotation;

Zafl_t::Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, bool p_use_stars, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_stars_analysis_engine(p_dbinterface),
	m_use_stars(p_use_stars),
	m_verbose(p_verbose)
{
        auto ed=ElfDependencies_t(getFileIR());
        (void)ed.appendLibraryDepedencies("libzafl.so");

        m_trace_map = ed.appendGotEntry("zafl_trace_map");
        m_prev_id = ed.appendGotEntry("zafl_prev_id");

	m_blacklistedFunctions.insert(".init_proc");
	m_blacklistedFunctions.insert("init");
	m_blacklistedFunctions.insert("_init");
	m_blacklistedFunctions.insert("fini");
	m_blacklistedFunctions.insert("_fini");
	m_blacklistedFunctions.insert("register_tm_clones");
	m_blacklistedFunctions.insert("deregister_tm_clones");
	m_blacklistedFunctions.insert("frame_dummy");
	m_blacklistedFunctions.insert("__do_global_dtors_aux");
}

static void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
        auto r=new Relocation_t(BaseObj_t::NOT_IN_DATABASE, wrt.second, "pcrel", wrt.first);
        fir->GetRelocations().insert(r);
        i->GetRelocations().insert(r);
}

static RegisterSet_t get_dead_regs(Instruction_t* insn, MEDS_AnnotationParser &meds_ap_param)
{
	MEDS_AnnotationParser *meds_ap=&meds_ap_param;

        assert(meds_ap);

        std::pair<MEDS_Annotations_t::iterator,MEDS_Annotations_t::iterator> ret;

        /* find it in the annotations */
        ret = meds_ap->getAnnotations().equal_range(insn->GetBaseID());
        MEDS_DeadRegAnnotation* p_annotation;

        /* for each annotation for this instruction */
        for (MEDS_Annotations_t::iterator it = ret.first; it != ret.second; ++it)
        {
                        p_annotation=dynamic_cast<MEDS_DeadRegAnnotation*>(it->second);
                        if(p_annotation==NULL)
                                continue;

                        /* bad annotation? */
                        if(!p_annotation->isValid())
                                continue;

                        return p_annotation->getRegisterSet();
        }

        /* couldn't find the annotation, return an empty set.*/
        return RegisterSet_t();
}

static bool areFlagsDead(Instruction_t* insn, MEDS_AnnotationParser &meds_ap_param)
{
	RegisterSet_t regset=get_dead_regs(insn, meds_ap_param);
	return (regset.find(MEDS_Annotation::rn_EFLAGS)!=regset.end());
}

static bool hasLeafAnnotation(Function_t* fn, MEDS_AnnotationParser &meds_ap_param)
{
	assert(fn);
        const auto ret = meds_ap_param.getFuncAnnotations().equal_range(fn->GetName());
	const auto sfa_it = find_if(ret.first, ret.second, [](const MEDS_Annotations_FuncPair_t &it)
		{
			auto p_annotation=dynamic_cast<MEDS_SafeFuncAnnotation*>(it.second);
			if(p_annotation==NULL)
				return false;
			return p_annotation->isLeaf();
		}
	);

	return (sfa_it != ret.second);
}

zafl_blockid_t Zafl_t::get_blockid(unsigned p_max_mask) 
{
	auto counter = 0;
	auto blockid = 0;

	while (counter++ < 100) {
		blockid = rand() % p_max_mask;
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
void Zafl_t::afl_instrument_bb(Instruction_t *inst, const bool p_hasLeafAnnotation)
{
	assert(inst);

	char buf[8192];
	auto tmp = inst;

	auto live_flags = true;

	if (m_use_stars)
		live_flags = !(areFlagsDead(inst, m_stars_analysis_engine.getAnnotations()));

	if (p_hasLeafAnnotation) 
	{
		// leaf function, must respect the red zone
		insertAssemblyBefore(getFileIR(), tmp, "lea rsp, [rsp-128]");
		tmp = insertAssemblyAfter(getFileIR(), tmp, "push rax");
	}
	else
	{
		insertAssemblyBefore(getFileIR(), tmp, "push rax");
	}

	tmp = insertAssemblyAfter(getFileIR(), tmp, "push rcx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "push rdx");

	const auto blockid = get_blockid();
	static unsigned labelid = 0; 
	labelid++;

	cout << "labelid: " << labelid << " baseid: " << inst->GetBaseID() << " instruction: " << inst->getDisassembly();
	if (live_flags)
	{
		cout << "   flags are live" << endl;
		tmp = insertAssemblyAfter(getFileIR(), tmp, "pushf"); 
	}
	else {
		cout << "   flags are dead" << endl;
	}


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

	if (live_flags) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "popf");
	}
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rdx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rcx");
	tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rax");
	if (p_hasLeafAnnotation) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "lea rsp, [rsp+128]");
	}

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

	if (m_use_stars)
		m_stars_analysis_engine.do_STARS(getFileIR());

	// for all functions
	//    for all basic blocks
	//          afl_instrument
	for (auto f : getFileIR()->GetFunctions())
	{
		if (!f) continue;
		if (f->GetName()[0] == '.' || m_blacklistedFunctions.find(f->GetName())!=m_blacklistedFunctions.end())
			continue;
		bool leafAnnotation = true;
		if (m_use_stars)
			leafAnnotation = hasLeafAnnotation(f, m_stars_analysis_engine.getAnnotations());
		if (f) 
		{
			if (leafAnnotation)
				cout << "Processing leaf function: ";
			else
				cout << "Processing function: ";
			cout << f->GetName() << endl;
		}

		auto current = num_bb_instrumented;
		ControlFlowGraph_t cfg(f);
		for (auto bb : cfg.GetBlocks())
		{
			afl_instrument_bb(bb->GetInstructions()[0], leafAnnotation);
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
