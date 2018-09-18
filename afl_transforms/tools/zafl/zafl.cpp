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

//#define OPTIMIZE_REGS

#include "zafl.hpp"

#include <stdlib.h>
#include <string.h> 
#include <algorithm>
#include <cctype>
#include <sstream>
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

Zafl_t::Zafl_t(libIRDB::pqxxDB_t &p_dbinterface, libIRDB::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_stars_analysis_engine(p_dbinterface),
	m_fork_server_entry(p_forkServerEntryPoint),
	m_exitpoints(p_exitPoints),
	m_use_stars(p_use_stars),
	m_autozafl(p_autozafl),
	m_bb_graph_optimize(false),
	m_verbose(p_verbose)
{
	if (m_use_stars) {
		cout << "Use STARS analysis engine" << endl;
		m_stars_analysis_engine.do_STARS(getFileIR());
	}

	auto ed=ElfDependencies_t(getFileIR());
	if (p_autozafl)
	{
		cout << "autozafl library is on" << endl;
		(void)ed.prependLibraryDepedencies("libautozafl.so");
	}
	else
	{
		cout << "autozafl library is off" << endl;
		(void)ed.prependLibraryDepedencies("libzafl.so");
	}

	m_plt_zafl_initAflForkServer=ed.appendPltEntry("zafl_initAflForkServer");
        m_trace_map = ed.appendGotEntry("zafl_trace_map");
        m_prev_id = ed.appendGotEntry("zafl_prev_id");

	// let's not instrument these functions ever
	m_blacklist.insert("init");
	m_blacklist.insert("_init");
	m_blacklist.insert("fini");
	m_blacklist.insert("_fini");
	m_blacklist.insert("register_tm_clones");
	m_blacklist.insert("deregister_tm_clones");
	m_blacklist.insert("frame_dummy");
	m_blacklist.insert("__do_global_ctors_aux");
	m_blacklist.insert("__do_global_dtors_aux");
	m_blacklist.insert("__libc_csu_init");
	m_blacklist.insert("__libc_csu_fini");
	m_blacklist.insert("start");
	m_blacklist.insert("__libc_start_main");
	m_blacklist.insert("__gmon_start__");
	m_blacklist.insert("__cxa_atexit");
	m_blacklist.insert("__cxa_finalize");
	m_blacklist.insert("__assert_fail");

	m_num_flags_saved = 0;
	m_num_temp_reg_saved = 0;
	m_num_tracemap_reg_saved = 0;
	m_num_previd_reg_saved = 0;
	m_num_bb_zero_predecessors = 0;
	m_num_bb_zero_successors = 0;
	m_num_bb_single_predecessors = 0;
	m_num_bb_single_successors = 0;
	m_num_bb_skipped = 0;
}

static void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
        auto r=new Relocation_t(BaseObj_t::NOT_IN_DATABASE, wrt.second, "pcrel", wrt.first);
        fir->GetRelocations().insert(r);
        i->GetRelocations().insert(r);
}

static RegisterSet_t get_dead_regs(Instruction_t* insn, MEDS_AnnotationParser &meds_ap_param)
{
        std::pair<MEDS_Annotations_t::iterator,MEDS_Annotations_t::iterator> ret;

        /* find it in the annotations */
        ret = meds_ap_param.getAnnotations().equal_range(insn->GetBaseID());
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

/*
 * Only allow instrumentation in whitelisted functions
 */
void Zafl_t::setWhitelist(const string& p_whitelist)
{
	std::ifstream whitelistFile(p_whitelist);
	if (!whitelistFile.is_open())
		throw;
	std::string line;
	while(whitelistFile >> line)
	{
		cout <<"Adding " << line << " to white list" << endl;
		m_whitelist.insert(line);
	}
	whitelistFile.close();
}

/*
 * Disallow instrumentation in blackListed functions
 */
void Zafl_t::setBlacklist(const string& p_blackList)
{
	std::ifstream blackListFile(p_blackList);
	if (!blackListFile.is_open())
		throw;
	std::string line;
	while(blackListFile >> line)
	{
		cout <<"Adding " << line << " to black list" << endl;
		m_blacklist.insert(line);
	}
	blackListFile.close();
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
#ifdef OPTIMIZE_REGS
static std::set<RegisterName> get_free_regs(const RegisterSet_t candidates)
{
	std::set<RegisterName> free_regs;
	const std::set<RegisterName> allowed_regs = {rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15};

	set_intersection(candidates.begin(),candidates.end(),free_regs.begin(),free_regs.end(),
                  std::inserter(free_regs,free_regs.begin()));

	return free_regs;
}
#endif

void Zafl_t::insertExitPoint(Instruction_t *inst)
{
	assert(inst);

	if (inst->GetFunction())
		cout << "in function: " << inst->GetFunction()->GetName() << " ";

	cout << "insert exit point at: 0x" << hex << inst->GetAddress()->GetVirtualOffset() << dec << " " << inst->getDisassembly() << endl;
	
	auto tmp = inst;
	     insertAssemblyBefore(tmp, "xor edi, edi"); //  rdi=0
	tmp = insertAssemblyAfter(tmp, "mov eax, 231"); //  231 = __NR_exit_group   from <asm/unistd_64.h>
	tmp = insertAssemblyAfter(tmp, "syscall");      //  sys_exit_group(edi)
}

/*
	Original afl instrumentation:
	        zafl_trace_bits[zafl_prev_id ^ id]++;
		zafl_prev_id = id >> 1;     
*/
void Zafl_t::afl_instrument_bb(Instruction_t *p_inst, const bool p_hasLeafAnnotation)
{
	assert(p_inst);

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
	auto tmp = p_inst;

	if (m_use_stars) 
	{
		live_flags = !(areFlagsDead(p_inst, m_stars_analysis_engine.getAnnotations()));
#ifdef OPTIMIZE_REGS
		auto regset = get_dead_regs(p_inst, m_stars_analysis_engine.getAnnotations());
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
#endif
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

	if (live_flags)
		m_num_flags_saved++;
	if (save_temp)
		m_num_temp_reg_saved++;
	if (save_trace_map)
		m_num_tracemap_reg_saved++;
	if (save_prev_id)
		m_num_previd_reg_saved++;

	//
	// warning: first instrumentation must use insertAssemblyBefore
	//

	auto inserted_before = false;
	if (p_hasLeafAnnotation) 
	{
		// leaf function, must respect the red zone
		insertAssemblyBefore(tmp, "lea rsp, [rsp-128]");
		inserted_before = true;
	}

	if (save_temp)
	{
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(tmp, "push rax");
		}
		else
		{
			insertAssemblyBefore(tmp, "push rax");
			inserted_before = true;
		}
	}

	if (save_trace_map)
	{
		if (inserted_before)
			tmp = insertAssemblyAfter(tmp, "push rcx");
		else
		{
			insertAssemblyBefore(tmp, "push rcx");
			inserted_before = true;
		}
	}

	if (save_prev_id)
	{
		if (inserted_before)
			tmp = insertAssemblyAfter(tmp, "push rdx");
		else
		{
			insertAssemblyBefore(tmp, "push rdx");
			inserted_before = true;
		}
	}

	const auto blockid = get_blockid();
	static unsigned labelid = 0; 
	labelid++;

	cerr << "labelid: " << labelid << " baseid: " << p_inst->GetBaseID() << " address: 0x" << hex << p_inst->GetAddress()->GetVirtualOffset() << dec << " instruction: " << p_inst->getDisassembly();

	if (live_flags)
	{
		cerr << "   flags are live" << endl;
		if (inserted_before)
			tmp = insertAssemblyAfter(tmp, "pushf"); 
		else
		{
			insertAssemblyBefore(tmp, "pushf"); 
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
		tmp = insertAssemblyAfter(tmp, buf);
	}
	else
	{
		insertAssemblyBefore(tmp, buf);
		inserted_before = true;
	}
	create_got_reloc(getFileIR(), m_prev_id, tmp);

//   7:   48 8b 0d 00 00 00 00    mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>
				sprintf(buf, "T%d: mov  %s, QWORD [rel T%d]", labelid, reg_trace_map, labelid); // rcx
	tmp = insertAssemblyAfter(tmp, buf);
	create_got_reloc(getFileIR(), m_trace_map, tmp);

//   e:   0f b7 02                movzx  eax,WORD PTR [rdx]                      
				sprintf(buf,"movzx  %s,WORD [%s]", reg_temp32, reg_prev_id);
	tmp = insertAssemblyAfter(tmp, buf);
//  11:   66 35 34 12             xor    ax,0x1234                              
				sprintf(buf, "xor   %s,0x%x", reg_temp16, blockid);
	tmp = insertAssemblyAfter(tmp, buf);
//  15:   0f b7 c0                movzx  eax,ax                                
				sprintf(buf,"movzx  %s,%s", reg_temp32, reg_temp16);
	tmp = insertAssemblyAfter(tmp, buf);
//  18:   48 03 01                add    rax,QWORD PTR [rcx]                  
				sprintf(buf,"add    %s,QWORD [%s]", reg_temp, reg_trace_map);
	tmp = insertAssemblyAfter(tmp, buf);                  
//  1b:   80 00 01                add    BYTE PTR [rax],0x1                  
				sprintf(buf,"add    BYTE [%s],0x1", reg_temp);
	tmp = insertAssemblyAfter(tmp, buf);                  
//  1e:   b8 1a 09 00 00          mov    eax,0x91a                          
				sprintf(buf, "mov   %s, 0x%x", reg_temp32, blockid >> 1);
	tmp = insertAssemblyAfter(tmp, buf);
	sprintf(buf,"baseid: %d labelid: %d", tmp->GetBaseID(), labelid);
//  23:   66 89 02                mov    WORD PTR [rdx],ax       
				sprintf(buf, "mov    WORD [%s], %s", reg_prev_id, reg_temp16);
	tmp = insertAssemblyAfter(tmp, buf);

	if (live_flags) 
	{
		tmp = insertAssemblyAfter(tmp, "popf");
	}

	if (save_prev_id) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rdx");
	}
	if (save_trace_map) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rcx");
	}
	if (save_temp) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rax");
	}

	if (p_hasLeafAnnotation) 
	{
		tmp = insertAssemblyAfter(tmp, "lea rsp, [rsp+128]");
	}
	
	free(reg_temp); 
	free(reg_temp32);
	free(reg_temp16);
	free(reg_trace_map);
	free(reg_prev_id);
}

// just give it a hash value!
void Zafl_t::afl_instrument_bb1(Instruction_t *p_inst, const bool p_hasLeafAnnotation)
{
	assert(p_inst);

	// @todo: placeholder for now
	assert(false);

	// collAfl optimization:
	//     for basic blocks with only 1 predecessor, 
	//     we can just use a slot in the tracemap directly

/*
	mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>   tracemap
	mov    rax, <hash_value>
	add    rax,QWORD PTR [rcx]                  
	add    BYTE PTR [rax],0x1                  
*/	
}

void Zafl_t::insertForkServer(Instruction_t* p_entry)
{
	assert(p_entry);

	stringstream ss;
	ss << "0x" << hex << p_entry->GetAddress()->GetVirtualOffset();
	cout << "inserting fork server code at address: " << ss.str() << dec << endl;
	if (p_entry->GetFunction())
		cout << " function: " << p_entry->GetFunction()->GetName();
	cout << endl;

	m_blacklist.insert(ss.str());

	// insert the instrumentation
	auto tmp=p_entry;
    	(void)insertAssemblyBefore(tmp," push rdi") ;
	tmp=  insertAssemblyAfter(tmp," push rsi ") ;
	tmp=  insertAssemblyAfter(tmp," push rbp") ;
	tmp=  insertAssemblyAfter(tmp," push rdx") ;
	tmp=  insertAssemblyAfter(tmp," push rcx ") ;
	tmp=  insertAssemblyAfter(tmp," push rbx ") ;
	tmp=  insertAssemblyAfter(tmp," push rax ") ;
	tmp=  insertAssemblyAfter(tmp," push r8 ") ;
	tmp=  insertAssemblyAfter(tmp," push r9 ") ;
	tmp=  insertAssemblyAfter(tmp," push r10 ") ;
	tmp=  insertAssemblyAfter(tmp," push r11 ") ;
	tmp=  insertAssemblyAfter(tmp," push r12 ") ;
	tmp=  insertAssemblyAfter(tmp," push r13 ") ;
	tmp=  insertAssemblyAfter(tmp," push r14 ") ;
	tmp=  insertAssemblyAfter(tmp," push r15 ") ;
	tmp=  insertAssemblyAfter(tmp," pushf ") ;
	tmp=  insertAssemblyAfter(tmp," call 0 ", m_plt_zafl_initAflForkServer) ;
	tmp=  insertAssemblyAfter(tmp," popf ") ;
	tmp=  insertAssemblyAfter(tmp," pop r15 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r14 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r13 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r12 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r11 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r10 ") ;
	tmp=  insertAssemblyAfter(tmp," pop r9");
	tmp=  insertAssemblyAfter(tmp," pop r8");
	tmp=  insertAssemblyAfter(tmp," pop rax");
	tmp=  insertAssemblyAfter(tmp," pop rbx");
	tmp=  insertAssemblyAfter(tmp," pop rcx");
	tmp=  insertAssemblyAfter(tmp," pop rdx");
	tmp=  insertAssemblyAfter(tmp," pop rbp");
	tmp=  insertAssemblyAfter(tmp," pop rsi");
	tmp=  insertAssemblyAfter(tmp," pop rdi");
}

void Zafl_t::insertForkServer(string p_forkServerEntry)
{
	assert(p_forkServerEntry.size() > 0);

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

	}

	// it's ok not to have a fork server at all, e.g. libraries
}

void Zafl_t::insertExitPoints()
{
	for (auto exitp : m_exitpoints)
	{
		if (std::isdigit(exitp[0]))
		{
			// find instruction to insert fork server based on address
			const auto voffset = (virtual_offset_t) std::strtoul(exitp.c_str(), NULL, 16);
			auto instructions=find_if(getFileIR()->GetInstructions().begin(), getFileIR()->GetInstructions().end(), [&](const Instruction_t* i) {
					return i->GetAddress()->GetVirtualOffset()==voffset;
				});

			if (instructions==getFileIR()->GetInstructions().end())
			{
				cerr << "Error: could not find address to insert exit point: " << exitp << endl;
				throw;
			}

			insertExitPoint(*instructions);
		}
		else
		{
			// find function
			auto func_iter=find_if(getFileIR()->GetFunctions().begin(), getFileIR()->GetFunctions().end(), [&](const Function_t* f) {
				return f->GetName()==exitp;
			});

		
			if(func_iter==getFileIR()->GetFunctions().end())
			{
				cerr << "Error: could not find function to insert exit points: " << exitp << endl;
				throw;
			}

			cout << "inserting fork server code at exit point of function: " << exitp << endl;
			for (auto i : (*func_iter)->GetInstructions())
			{
				// if it's a return instruction, instrument exit point
				const auto d=DecodedInstruction_t(i);
				if (d.isReturn())
				{
					insertExitPoint(i);
				}
			}
		}
	}
}


bool Zafl_t::isBlacklisted(const Function_t *p_func) const
{
	return (p_func->GetName()[0] == '.' || 
	        p_func->GetName().find("@plt") != string::npos ||
	        m_blacklist.find(p_func->GetName())!=m_blacklist.end());
}

bool Zafl_t::isWhitelisted(const Function_t *p_func) const
{
	if (m_whitelist.size() == 0) return true;
	return (m_whitelist.find(p_func->GetName())!=m_whitelist.end());
}

bool Zafl_t::isBlacklisted(const Instruction_t *p_inst) const
{
	const auto vo = p_inst->GetAddress()->GetVirtualOffset();
	char tmp[1024];
	snprintf(tmp, 1000, "0x%x", (unsigned int)vo);
	return (m_blacklist.count(tmp) > 0 || isBlacklisted(p_inst->GetFunction()));
}

bool Zafl_t::isWhitelisted(const Instruction_t *p_inst) const
{
	if (m_whitelist.size() == 0) return true;

	const auto vo = p_inst->GetAddress()->GetVirtualOffset();
	char tmp[1024];
	snprintf(tmp, 1000, "0x%x", (unsigned int)vo);
	return (m_whitelist.count(tmp) > 0 || isWhitelisted(p_inst->GetFunction()));
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

	setupForkServer();

	insertExitPoints();

	struct BaseIDSorter
	{
	    bool operator()( const Function_t* lhs, const Function_t* rhs ) const {
		return lhs->GetBaseID() < rhs->GetBaseID();
	    }
	};

	auto bb_id = -1;
	auto num_bb = 0;
	auto num_bb_zero_preds_entry_point = 0;

	// for all functions
	//    for all basic blocks
	//          afl_instrument
	set<Function_t*, BaseIDSorter> sortedFuncs(getFileIR()->GetFunctions().begin(), getFileIR()->GetFunctions().end());
	for_each( sortedFuncs.begin(), sortedFuncs.end(), [&](Function_t* f)
	{
		if (!f) return;
		// skip instrumentation for blacklisted functions 
		if (isBlacklisted(f)) return;
		// skip if function has no entry point
		if (!f->GetEntryPoint())
			return;

		bool leafAnnotation = true;
		if (m_use_stars) 
		{
			leafAnnotation = hasLeafAnnotation(f, m_stars_analysis_engine.getAnnotations());
		}

		cout << endl;

		ControlFlowGraph_t cfg(f);
		auto num_bb_instrumented_this_function = 0;
		const auto num_blocks = cfg.GetBlocks().size();
		num_bb += num_blocks;

		if (leafAnnotation)
			cout << "Processing leaf function: ";
		else
			cout << "Processing function: ";
		cout << f->GetName();
		cout << " " << num_blocks << " basic blocks" << endl;

//		cout << cfg << endl;

		for (auto bb : cfg.GetBlocks())
		{
			assert(bb->GetInstructions().size() > 0);

			bb_id++;

			if (m_verbose)
			{
				cout << "---" << endl;
				cout << "basic block id#" << bb_id << " has " << bb->GetInstructions().size() << " instructions";
				cout << " instr: " << bb->GetInstructions()[0]->getDisassembly();
				if (bb->GetInstructions()[0]->GetIndirectBranchTargetAddress())
					cout << " ibta";
				cout << " | preds: " << bb->GetPredecessors().size() << " succs: " << bb->GetSuccessors().size();
				if (bb->GetPredecessors().size()==1)
				{
					const auto pred = *(bb->GetPredecessors().begin());
					cout << " succ(pred): " << pred->GetSuccessors().size();
					if (pred->GetSuccessors().size() == 2)
					{
						auto num_instruction_in_prev_bb = pred->GetInstructions().size();
						cout << " last_instr_in_pred: " <<
							pred->GetInstructions()[num_instruction_in_prev_bb-1]->getDisassembly();
					}
				}
				cout << endl;
			}

			// if whitelist specified, only allow instrumentation for functions/addresses in whitelist
			if (m_whitelist.size() > 0) {
				if (!isWhitelisted(bb->GetInstructions()[0]))
					continue;
			}

			if (isBlacklisted(bb->GetInstructions()[0]))
				continue;


			// debugging support
			if (getenv("ZAFL_LIMIT_BEGIN"))
			{
				if (bb_id < atoi(getenv("ZAFL_LIMIT_BEGIN")))
					continue;	
			}

			// debugging support
			if (getenv("ZAFL_LIMIT_END"))
			{
				if (bb_id >= atoi(getenv("ZAFL_LIMIT_END"))) 
					continue;
			}

			// collect stats
			if (bb->GetPredecessors().size() == 0)
				m_num_bb_zero_predecessors++;
			if (bb->GetPredecessors().size() == 1)
				m_num_bb_single_predecessors++;
			if (bb->GetSuccessors().size() == 0)
				m_num_bb_zero_successors++;
			if (bb->GetSuccessors().size() == 1)
				m_num_bb_single_successors++;
			if (bb->GetInstructions()[0] == f->GetEntryPoint())
				num_bb_zero_preds_entry_point++;
			
			// optimization:
			//        bb is not an IBTA
			//        bb has 1 predecessor 
			//        predecessor has only 1 successor (namely this bb)
			// nb: useless, on objdump happens 10/16500 bbs
			// nb2: relax ibta restriction, provided prev bb is an exit block (e.g. call)
			if (m_bb_graph_optimize) //  && !bb->GetInstructions()[0]->GetIndirectBranchTargetAddress())
			{
				if (bb->GetPredecessors().size() == 1)
				{
					const auto pred = *(bb->GetPredecessors().begin());
					if (pred->GetSuccessors().size() == 1)
					{
						if (!bb->GetInstructions()[0]->GetIndirectBranchTargetAddress())
						{
							cout << "Skipping basic block #" << dec << bb_id << " because not ibta, <1,*> and preds <*,1>" << endl;
							m_num_bb_skipped++;
							continue;
						} 
						else if (pred->GetIsExitBlock())
						{
							cout << "Skipping basic block #" << dec << bb_id << " because <1,*> and preds(ibta+exit_block) <*,1>" << endl;
							m_num_bb_skipped++;
							continue;
						}
					}
				}
			}


			// optimization (padding nop)
			// bb with 1 instruction (nop), 0 preds, 1 succ ???
			// @todo: make sure it's a no-op
			if (bb->GetInstructions().size()==1 && bb->GetPredecessors().size()==0 && bb->GetSuccessors().size()==1)
			{
				//  @todo: verify it's a no-op
				cout << "Skipping basic block #" << dec << bb_id << " because it's a padding instruction" << endl;
				m_num_bb_skipped++;
				continue;
			}

			// optimization conditional branch
			// can elide conditional branch, provided keep both outgoing edges
			// about 1500 of these in objdump

			cout << "Instrumenting basic block #" << dec << bb_id << endl;
			num_bb_instrumented_this_function++;

			if (m_bb_graph_optimize)
			{
				if (bb->GetPredecessors().size() == 1)
				{
					// optimization: @todo
					//	bb has exactly 1 predecessor: just give it a hash (CollAFL-lite)
					afl_instrument_bb(bb->GetInstructions()[0], leafAnnotation);
				}
				else
				{
					afl_instrument_bb(bb->GetInstructions()[0], leafAnnotation);
				}
			}
			else 
			{
				afl_instrument_bb(bb->GetInstructions()[0], leafAnnotation);
			}
		}
		
		num_bb_instrumented += num_bb_instrumented_this_function;
		if (f) {
			cout << "Function " << f->GetName() << " :  " << dec << num_bb_instrumented_this_function << "/" << cfg.GetBlocks().size() << " basic blocks instrumented." << endl;
		}
	});

	// count orphan instructions
	for (auto i : getFileIR()->GetInstructions())
	{
		if (i && (i->GetFunction() == NULL))		
			num_orphan_instructions++;
	}

	cout << "#ATTRIBUTE num_bb_instrumented=" << dec << num_bb_instrumented << endl;
	cout << "#ATTRIBUTE num_orphan_instructions=" << dec << num_orphan_instructions << endl;
	cout << "#ATTRIBUTE num_flags_saved=" << m_num_flags_saved << endl;
	cout << "#ATTRIBUTE num_temp_reg_saved=" << m_num_temp_reg_saved << endl;
	cout << "#ATTRIBUTE num_tracemap_reg_saved=" << m_num_tracemap_reg_saved << endl;
	cout << "#ATTRIBUTE num_previd_reg_saved=" << m_num_previd_reg_saved << endl;
	cout << "#ATTRIBUTE num_bb=" << dec << num_bb << endl;
	cout << "#ATTRIBUTE num_bb_zero_predecessors_entry_point=" << dec << num_bb_zero_preds_entry_point << endl;
	cout << "#ATTRIBUTE num_bb_zero_predecessors=" << dec << m_num_bb_zero_predecessors << endl;
	cout << "#ATTRIBUTE num_bb_zero_successors=" << dec << m_num_bb_zero_successors << endl;
	cout << "#ATTRIBUTE num_bb_single_predecessors=" << dec << m_num_bb_single_predecessors << endl;
	cout << "#ATTRIBUTE num_bb_single_successors=" << dec << m_num_bb_single_successors << endl;
	cout << "#ATTRIBUTE num_bb_skipped=" << dec << m_num_bb_skipped << endl;

	return 1;
}
