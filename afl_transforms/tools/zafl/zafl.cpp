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
#include <string.h> 
#include <algorithm>
#include <cctype>
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

Zafl_t::Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_forkServerEntryPoint, bool p_use_stars, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_stars_analysis_engine(p_dbinterface),
	m_fork_server_entry(p_forkServerEntryPoint),
	m_use_stars(p_use_stars),
	m_verbose(p_verbose)
{
	auto ed=ElfDependencies_t(getFileIR());
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

// return intersection of candidates and allowed general-purpose registers
static std::set<RegisterName> get_free_regs(const RegisterSet_t candidates)
{
	std::set<RegisterName> free_regs;
	const std::set<RegisterName> allowed_regs = {rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15};

	set_intersection(candidates.begin(),candidates.end(),free_regs.begin(),free_regs.end(),
                  std::inserter(free_regs,free_regs.begin()));

	return free_regs;
}

/*
        zafl_trace_bits[zafl_prev_id ^ id]++;
        zafl_prev_id = id >> 1;     
*/
void Zafl_t::afl_instrument_bb(Instruction_t *inst, const bool p_hasLeafAnnotation)
{
	assert(inst);

	char buf[8192];
	auto live_flags = true;
	char *reg_temp = NULL;
	char *reg_temp32 = NULL;
	char *reg_temp16 = NULL;
	char *reg_trace_map = NULL;
	char *reg_prev_id = NULL;
	auto save_temp = true;
	auto save_trace_map = true;
	auto save_prev_id = true;
	auto tmp = inst;

	if (m_use_stars) 
	{
		live_flags = !(areFlagsDead(inst, m_stars_analysis_engine.getAnnotations()));
		auto regset = get_dead_regs(inst, m_stars_analysis_engine.getAnnotations());
		auto free_regs = get_free_regs(regset);

		for (auto r : regset)
		{
			if (r == rn_RAX)
			{
				reg_temp = strdup("rax"); reg_temp32 = strdup("eax"); reg_temp16 = strdup("ax");
				save_temp = false;
				free_regs.erase(r);
			}
			else if (r == rn_RCX)
			{
				reg_trace_map = strdup("rcx");
				save_trace_map = false;
				free_regs.erase(r);
			}
			else if (r == rn_RDX)
			{
				reg_prev_id = strdup("rdx");
				save_prev_id = false;
				free_regs.erase(r);
			}
		}

		if (save_temp && free_regs.size() >= 1) 
		{
			auto r = *free_regs.begin(); 
			auto r32 = Register::demoteTo32(r); assert(Register::isValidRegister(r32));
			auto r16 = Register::demoteTo16(r); assert(Register::isValidRegister(r16));
			reg_temp = strdup(Register::toString(r).c_str());
			reg_temp32 = strdup(Register::toString(r32).c_str());
			reg_temp16 = strdup(Register::toString(r16).c_str());
			save_temp = false;
			free_regs.erase(r);
		}

		if (save_trace_map && free_regs.size() >= 1) 
		{
			auto r = *free_regs.begin();
			reg_trace_map = strdup(Register::toString(r).c_str());
			save_trace_map = false;
			free_regs.erase(r);
		}
		if (save_prev_id && free_regs.size() >= 1) 
		{
			auto r = *free_regs.begin();
			reg_prev_id = strdup(Register::toString(r).c_str());
			save_prev_id = false;
			free_regs.erase(r);
		}
	}

	if (!reg_temp)
		reg_temp = strdup("rax");
	if (!reg_temp32)
		reg_temp32 = strdup("eax");
	if (!reg_temp16)
		reg_temp16 = strdup("ax");
	if (!reg_trace_map)
		reg_trace_map = strdup("rcx");
	if (!reg_prev_id)
		reg_prev_id = strdup("rdx");

	cerr << "save_temp: " << save_temp << " save_trace_map: " << save_trace_map << " save_prev_id: " << save_prev_id << " live_flags: " << live_flags << endl;
	cerr << "reg_temp: " << reg_temp << " " << reg_temp32 << " " << reg_temp16 
		<< " reg_trace_map: " << reg_trace_map
		<< " reg_prev_id: " << reg_prev_id << endl;

	//
	// warning: first instrumentation must use insertAssemblyBefore
	//

	auto inserted_before = false;
	if (p_hasLeafAnnotation) 
	{
		// leaf function, must respect the red zone
		insertAssemblyBefore(getFileIR(), tmp, "lea rsp, [rsp-128]");
		inserted_before = true;
	}

	if (save_temp)
	{
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(getFileIR(), tmp, "push rax");
		}
		else
		{
			insertAssemblyBefore(getFileIR(), tmp, "push rax");
			inserted_before = true;
		}
	}

	if (save_trace_map)
	{
		if (inserted_before)
			tmp = insertAssemblyAfter(getFileIR(), tmp, "push rcx");
		else
		{
			insertAssemblyBefore(getFileIR(), tmp, "push rcx");
			inserted_before = true;
		}
	}

	if (save_prev_id)
	{
		if (inserted_before)
			tmp = insertAssemblyAfter(getFileIR(), tmp, "push rdx");
		else
		{
			insertAssemblyBefore(getFileIR(), tmp, "push rdx");
			inserted_before = true;
		}
	}

	const auto blockid = get_blockid();
	static unsigned labelid = 0; 
	labelid++;

	cerr << "labelid: " << labelid << " baseid: " << inst->GetBaseID() << " instruction: " << inst->getDisassembly();

	if (live_flags)
	{
		cerr << "   flags are live" << endl;
		if (inserted_before)
			tmp = insertAssemblyAfter(getFileIR(), tmp, "pushf"); 
		else
		{
			insertAssemblyBefore(getFileIR(), tmp, "pushf"); 
			inserted_before = true;
		}
	}
	else {
		cerr << "   flags are dead" << endl;
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

//   0:   48 8b 15 00 00 00 00    mov    rdx,QWORD PTR [rip+0x0]        # 7 <f+0x7>
				sprintf(buf, "P%d: mov  %s, QWORD [rel P%d]", labelid, reg_prev_id, labelid); // rdx
	if (inserted_before)
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	}
	else
	{
		insertAssemblyBefore(getFileIR(), tmp, buf);
		inserted_before = true;
	}
	create_got_reloc(getFileIR(), m_prev_id, tmp);

//   7:   48 8b 0d 00 00 00 00    mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>
				sprintf(buf, "T%d: mov  %s, QWORD [rel T%d]", labelid, reg_trace_map, labelid); // rcx
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	create_got_reloc(getFileIR(), m_trace_map, tmp);

//   e:   0f b7 02                movzx  eax,WORD PTR [rdx]                      
				sprintf(buf,"movzx  %s,WORD [%s]", reg_temp32, reg_prev_id);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
//  11:   66 35 34 12             xor    ax,0x1234                              
				sprintf(buf, "xor   %s,0x%x", reg_temp16, blockid);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
//  15:   0f b7 c0                movzx  eax,ax                                
				sprintf(buf,"movzx  %s,%s", reg_temp32, reg_temp16);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
//  18:   48 03 01                add    rax,QWORD PTR [rcx]                  
				sprintf(buf,"add    %s,QWORD [%s]", reg_temp, reg_trace_map);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);                  
//  1b:   80 00 01                add    BYTE PTR [rax],0x1                  
				sprintf(buf,"add    BYTE [%s],0x1", reg_temp);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);                  
//  1e:   b8 1a 09 00 00          mov    eax,0x91a                          
				sprintf(buf, "mov   %s, 0x%x", reg_temp32, blockid >> 1);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	sprintf(buf,"baseid: %d labelid: %d", tmp->GetBaseID(), labelid);
//  23:   66 89 02                mov    WORD PTR [rdx],ax       
				sprintf(buf, "mov    WORD [%s], %s", reg_prev_id, reg_temp16);
	tmp = insertAssemblyAfter(getFileIR(), tmp, buf);
	sprintf(buf,"baseid: %d labelid: %d", tmp->GetBaseID(), labelid);

	if (live_flags) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "popf");
	}
	if (save_prev_id) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rdx");
	}
	if (save_trace_map) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rcx");
	}
	if (save_temp) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "pop rax");
	}
	if (p_hasLeafAnnotation) 
	{
		tmp = insertAssemblyAfter(getFileIR(), tmp, "lea rsp, [rsp+128]");
	}
	
	free(reg_temp); 
	free(reg_temp32);
	free(reg_temp16);
	free(reg_trace_map);
	free(reg_prev_id);
}

void Zafl_t::insertForkServer(Instruction_t* p_entry)
{
	assert(p_entry);

	cout << "inserting fork server code at address: " << hex << p_entry->GetAddress()->GetVirtualOffset() << dec;
	if (p_entry->GetFunction())
		cout << " function: " << p_entry->GetFunction()->GetName();
	cout << endl;

	// insert the PLT needed
	auto ed=ElfDependencies_t(getFileIR());
	(void)ed.appendLibraryDepedencies("libzafl.so");
	auto edafl=ed.appendPltEntry("zafl_initAflForkServer");

	// insert the instrumentation
	auto tmp=p_entry;
    	(void)insertAssemblyBefore(getFileIR(),tmp," push rdi") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rsi ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rbp") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rdx") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rcx ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rbx ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push rax ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r8 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r9 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r10 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r11 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r12 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r13 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r14 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," push r15 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pushf ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," call 0 ", edafl) ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," popf ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r15 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r14 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r13 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r12 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r11 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r10 ") ;
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r9");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop r8");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rax");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rbx");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rcx");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rdx");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rbp");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rsi");
	tmp=  insertAssemblyAfter(getFileIR(), tmp," pop rdi");
}

void Zafl_t::insertForkServer(string p_forkServerEntry)
{
	assert(p_forkServerEntry.size() >= 1);

	cout << "looking for fork server entry point: " << p_forkServerEntry << endl;

	if (std::isdigit(p_forkServerEntry[0]))
	{
		// find instruction to insert fork server based on address
		const auto voffset = (virtual_offset_t) std::strtoul(p_forkServerEntry.c_str(), NULL, 16);
		auto instructions=find_if(getFileIR()->GetInstructions().begin(), getFileIR()->GetInstructions().end(), [&](const Instruction_t* i) {
				return i->GetAddress()->GetVirtualOffset()==voffset;
			});

		if (instructions==getFileIR()->GetInstructions().end())
		{
			cerr << "Error: could not find address to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		insertForkServer(*instructions);
	}
	else
	{
		// find entry point of specified function to insert fork server
		auto entryfunc=find_if(getFileIR()->GetFunctions().begin(), getFileIR()->GetFunctions().end(), [&](const Function_t* f) {
				return f->GetName()==p_forkServerEntry;
			});

		
		if(entryfunc==getFileIR()->GetFunctions().end())
		{
			cerr << "Error: could not find function to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		cout << "inserting fork server code at entry point of function: " << p_forkServerEntry << endl;
		auto entrypoint = (*entryfunc)->GetEntryPoint();
		insertForkServer(entrypoint);
	}
}

void Zafl_t::setupForkServer()
{
	if (m_fork_server_entry.size()>0)
	{
		// user has specified entry point
		insertForkServer(m_fork_server_entry);
	}
	else
	{
		// try to insert fork server at main
		const auto &all_funcs=getFileIR()->GetFunctions();
		const auto main_func_it=find_if(all_funcs.begin(), all_funcs.end(), [&](const Function_t* f) { return f->GetName()=="main";});
		if(main_func_it!=all_funcs.end())
		{
			insertForkServer("main"); 
		}
		else
		{
			// if no main then use autozafl, which automatically sets up a fork server 
        		auto ed=ElfDependencies_t(getFileIR());
		        (void)ed.appendLibraryDepedencies("libautozafl.so");
		}
	}
}

bool Zafl_t::isBlacklisted(const Function_t *p_func) const
{
	return (p_func->GetName()[0] == '.' || m_blacklistedFunctions.find(p_func->GetName())!=m_blacklistedFunctions.end());
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

	setupForkServer();

	// for all functions
	//    for all basic blocks
	//          afl_instrument
	for (auto f : getFileIR()->GetFunctions())
	{
		if (!f) continue;
		if (isBlacklisted(f) || !f->GetEntryPoint())
			continue;
		bool leafAnnotation = true;
		if (m_use_stars)
			leafAnnotation = hasLeafAnnotation(f, m_stars_analysis_engine.getAnnotations());

		cout << endl;
		if (leafAnnotation)
			cout << "Processing leaf function: ";
		else
			cout << "Processing function: ";
		cout << f->GetName() << endl;

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
