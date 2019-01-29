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

#include "zax.hpp"

using namespace std;
using namespace libTransform;
using namespace IRDB_SDK;
using namespace Zafl;
using namespace IRDBUtility;
using namespace MEDS_Annotation;

Zax_t::Zax_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl, bool p_verbose)
	:
	Transform(NULL, p_variantIR, NULL),
	m_dbinterface(p_dbinterface),
	m_stars_analysis_engine(p_dbinterface),
	m_fork_server_entry(p_forkServerEntryPoint),
	m_exitpoints(p_exitPoints),
	m_use_stars(p_use_stars),
	m_autozafl(p_autozafl),
	m_bb_graph_optimize(false),
	m_forkserver_enabled(true),
	m_breakupCriticalEdges(false),
	m_verbose(p_verbose)
{
	if (m_use_stars) {
		cout << "Use STARS analysis engine" << endl;
		m_stars_analysis_engine.do_STARS(getFileIR());
	}

	auto ed=libIRDB::ElfDependencies_t(getFileIR());
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

	if (m_verbose)
		cout << "verbose mode is on" << endl;
	else
		cout << "verbose mode is off" << endl;

	// bind to external symbols declared in libzafl.so
	m_plt_zafl_initAflForkServer=ed.appendPltEntry("zafl_initAflForkServer");
        m_trace_map = ed.appendGotEntry("zafl_trace_map");
        m_prev_id = ed.appendGotEntry("zafl_prev_id");

	// let's not instrument these functions ever
	// see isBlacklisted() for other blacklisted functions
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

	m_labelid = 0;

	m_num_bb = 0;
	m_num_bb_instrumented = 0;
	m_num_bb_skipped = 0;
	m_num_bb_skipped_pushjmp = 0;
	m_num_bb_skipped_nop_padding = 0;
	m_num_bb_skipped_innernode = 0;
	m_num_bb_skipped_cbranch = 0;
	m_num_bb_skipped_onlychild = 0;
	m_num_bb_keep_exit_block = 0;
	m_num_bb_keep_cbranch_back_edge = 0;
	m_num_style_collafl = 0;
}

void Zax_t::setBreakupCriticalEdges(const bool p_breakupEdges)
{
	m_breakupCriticalEdges = p_breakupEdges;
}


void create_got_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
	/*
        auto r=new Relocation_t(BaseObj_t::NOT_IN_DATABASE, wrt.second, "pcrel", wrt.first);
        fir->getRelocations().insert(r);
        i->getRelocations().insert(r);
	*/
	(void)fir->addNewRelocation(i,wrt.second, "pcrel", wrt.first);
}

RegisterSet_t get_dead_regs(Instruction_t* insn, MEDS_AnnotationParser &meds_ap_param)
{
        std::pair<MEDS_Annotations_t::iterator,MEDS_Annotations_t::iterator> ret;

        /* find it in the annotations */
        ret = meds_ap_param.getAnnotations().equal_range(insn->getBaseID());
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

// return intersection of candidates and allowed general-purpose registers
RegisterSet_t get_free_regs(const RegisterSet_t candidates, const RegisterSet_t allowed)
{
	std::set<RegisterName> free_regs;
	set_intersection(candidates.begin(),candidates.end(),allowed.begin(),allowed.end(),
                  std::inserter(free_regs,free_regs.begin()));
	return free_regs;
}

static bool hasLeafAnnotation(Function_t* fn, MEDS_AnnotationParser &meds_ap_param)
{
	assert(fn);
        const auto ret = meds_ap_param.getFuncAnnotations().equal_range(fn->getName());
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

bool Zax_t::BB_isPaddingNop(const libIRDB::BasicBlock_t *p_bb)
{
	return p_bb->GetInstructions().size()==1 && 
	       p_bb->GetPredecessors().size()==0 &&
	       p_bb->GetSuccessors().size()==1 &&
	       p_bb->GetInstructions()[0]->getDisassembly().find("nop")!=string::npos;
}

bool Zax_t::BB_isPushJmp(const libIRDB::BasicBlock_t *p_bb)
{
	return p_bb->GetInstructions().size()==2 && 
	       p_bb->GetInstructions()[0]->getDisassembly().find("push")!=string::npos &&
	       p_bb->GetInstructions()[1]->getDisassembly().find("jmp")!=string::npos;
}

/*
 * Only allow instrumentation in whitelisted functions/instructions
 * Each line in file is either a function name or address
 */
void Zax_t::setWhitelist(const string& p_whitelist)
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
 * Disallow instrumentation in blacklisted functions/instructions
 * Each line in file is either a function name or address
 */
void Zax_t::setBlacklist(const string& p_blackList)
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

zafl_blockid_t Zax_t::get_blockid(const unsigned p_max) 
{
	auto counter = 0;
	auto blockid = 0;

	// only try getting new block id 100 times
	// avoid returning duplicate if we can help it
	while (counter++ < 100) {
		blockid = rand() % p_max; 
		if (m_used_blockid.find(blockid) == m_used_blockid.end())
		{
			m_used_blockid.insert(blockid);
			return blockid;
		}
	}
	return blockid;
}

zafl_labelid_t Zax_t::get_labelid(const unsigned p_max) 
{
	return m_labelid++;
}

void Zax_t::insertExitPoint(Instruction_t *p_inst)
{
	assert(p_inst->getAddress()->getVirtualOffset());

	if (p_inst->getFunction())
		cout << "in function: " << p_inst->getFunction()->getName() << " ";

	stringstream ss;
	ss << hex << p_inst->getAddress()->getVirtualOffset();

	cout << "insert exit point at: 0x" << ss.str() << endl;
	m_blacklist.insert(ss.str());
	
	auto tmp = p_inst;
	     insertAssemblyBefore(tmp, "xor edi, edi"); //  rdi=0
	tmp = insertAssemblyAfter(tmp, "mov eax, 231"); //  231 = __NR_exit_group   from <asm/unistd_64.h>
	tmp = insertAssemblyAfter(tmp, "syscall");      //  sys_exit_group(edi)
//	insertAssemblyBefore(p_inst, "hlt"); 
}

/*
	Original afl instrumentation:
	        block_id = <random>;
	        zafl_trace_bits[zafl_prev_id ^ block_id]++;
		zafl_prev_id = block_id >> 1;     

	CollAfl optimization when (#predecessors==1) (goal of CollAfl is to reduce collisions):
	        block_id = <some unique value for this block>
	        zafl_trace_bits[block_id]++;
		zafl_prev_id = block_id >> 1;     
*/
void Zax_t::afl_instrument_bb(Instruction_t *p_inst, const bool p_hasLeafAnnotation, const bool p_collafl_optimization)
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

	BBRecord_t block_record;

	block_record.push_back(p_inst);

	if (m_use_stars) 
	{
		auto regset = get_dead_regs(p_inst, m_stars_analysis_engine.getAnnotations());
		live_flags = regset.find(MEDS_Annotation::rn_EFLAGS)==regset.end();
		const RegisterSet_t allowed_regs = {rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15};

		auto free_regs = get_free_regs(regset, allowed_regs);

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

	if (m_verbose)
	{
		cout << "save_temp: " << save_temp << " save_trace_map: " << save_trace_map << " save_prev_id: " << save_prev_id << " live_flags: " << live_flags << endl;
		cout << "reg_temp: " << reg_temp << " " << reg_temp32 << " " << reg_temp16 
			<< " reg_trace_map: " << reg_trace_map
			<< " reg_prev_id: " << reg_prev_id << endl;
	}

	// warning: first instrumentation must use insertAssemblyBefore
	auto inserted_before = false;
	if (p_hasLeafAnnotation) 
	{
		// leaf function, must respect the red zone
		const auto orig = insertAssemblyBefore(tmp, "lea rsp, [rsp-128]");
		inserted_before = true;
		block_record.push_back(orig);
	}

	if (save_temp)
	{
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(tmp, "push rax");
			block_record.push_back(tmp);
		}
		else
		{
			const auto orig = insertAssemblyBefore(tmp, "push rax");
			inserted_before = true;
			block_record.push_back(orig);
		}
	}

	if (save_trace_map)
	{
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(tmp, "push rcx");
			block_record.push_back(tmp);
		}
		else
		{
			const auto orig = insertAssemblyBefore(tmp, "push rcx");
			inserted_before = true;
			block_record.push_back(orig);
		}
	}

	if (save_prev_id)
	{
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(tmp, "push rdx");
			block_record.push_back(tmp);
		}
		else
		{
			const auto orig = insertAssemblyBefore(tmp, "push rdx");
			inserted_before = true;
			block_record.push_back(orig);
		}
	}

	const auto blockid = get_blockid();
	const auto labelid = get_labelid(); 

	if (m_verbose)
	{
		cout << "labelid: " << labelid << " baseid: " << p_inst->getBaseID() << " address: 0x" << hex << p_inst->getAddress()->getVirtualOffset() << dec << " instruction: " << p_inst->getDisassembly();
	}

	if (live_flags)
	{
		if (m_verbose) 
			cout << "   flags are live" << endl;
		if (inserted_before)
		{
			tmp = insertAssemblyAfter(tmp, "pushf"); 
			block_record.push_back(tmp);
		}
		else
		{
			const auto orig = insertAssemblyBefore(tmp, "pushf"); 
			inserted_before = true;
			block_record.push_back(orig);
		}
	}
	else {
		if (m_verbose)
			cout << "   flags are dead" << endl;
	}


/*
   0:   48 8b 15 00 00 00 00    mov    rdx,QWORD PTR [rip+0x0]        # 7 <f+0x7>
   7:   48 8b 0d 00 00 00 00    mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>

<no collafl optimization>
   e:   0f b7 02                movzx  eax,WORD PTR [rdx]                      
  11:   66 35 34 12             xor    ax,0x1234                              
  15:   0f b7 c0                movzx  eax,ax                                

<collafl-style optimization>
                                mov    eax, <blockid>

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
		block_record.push_back(tmp);
	}
	else
	{
		const auto orig = insertAssemblyBefore(tmp, buf);
		inserted_before = true;
		block_record.push_back(orig);
	}
	create_got_reloc(getFileIR(), m_prev_id, tmp);

//   7:   48 8b 0d 00 00 00 00    mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>
				sprintf(buf, "T%d: mov  %s, QWORD [rel T%d]", labelid, reg_trace_map, labelid); // rcx
	tmp = insertAssemblyAfter(tmp, buf);
	create_got_reloc(getFileIR(), m_trace_map, tmp);
	block_record.push_back(tmp);

	if (!p_collafl_optimization)
	{
//   e:   0f b7 02                movzx  eax,WORD PTR [rdx]                      
				sprintf(buf,"movzx  %s,WORD [%s]", reg_temp32, reg_prev_id);
		tmp = insertAssemblyAfter(tmp, buf);
		block_record.push_back(tmp);
//  11:   66 35 34 12             xor    ax,0x1234                              
				sprintf(buf, "xor   %s,0x%x", reg_temp16, blockid);
		tmp = insertAssemblyAfter(tmp, buf);
		block_record.push_back(tmp);
//  15:   0f b7 c0                movzx  eax,ax                                
				sprintf(buf,"movzx  %s,%s", reg_temp32, reg_temp16);
		tmp = insertAssemblyAfter(tmp, buf);
		block_record.push_back(tmp);
	}
	else
	{
//                                mov    rax, <blockid>
				sprintf(buf,"mov   %s,0x%x", reg_temp, blockid);
		tmp = insertAssemblyAfter(tmp, buf);
		block_record.push_back(tmp);
	}

//  18:   48 03 01                add    rax,QWORD PTR [rcx]                  
				sprintf(buf,"add    %s,QWORD [%s]", reg_temp, reg_trace_map);
	tmp = insertAssemblyAfter(tmp, buf);                  
	block_record.push_back(tmp);
//  1b:   80 00 01                add    BYTE PTR [rax],0x1                  
				sprintf(buf,"add    BYTE [%s],0x1", reg_temp);
	tmp = insertAssemblyAfter(tmp, buf);                  
	block_record.push_back(tmp);
//  1e:   b8 1a 09 00 00          mov    eax,0x91a                          
				sprintf(buf, "mov   %s, 0x%x", reg_temp32, blockid >> 1);
	tmp = insertAssemblyAfter(tmp, buf);
	block_record.push_back(tmp);
	sprintf(buf,"baseid: %d labelid: %d", tmp->getBaseID(), labelid);
//  23:   66 89 02                mov    WORD PTR [rdx],ax       
				sprintf(buf, "mov    WORD [%s], %s", reg_prev_id, reg_temp16);
	tmp = insertAssemblyAfter(tmp, buf);
	block_record.push_back(tmp);

	if (live_flags) 
	{
		tmp = insertAssemblyAfter(tmp, "popf");
		block_record.push_back(tmp);
	}

	if (save_prev_id) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rdx");
		block_record.push_back(tmp);
	}
	if (save_trace_map) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rcx");
		block_record.push_back(tmp);
	}
	if (save_temp) 
	{
		tmp = insertAssemblyAfter(tmp, "pop rax");
		block_record.push_back(tmp);
	}

	if (p_hasLeafAnnotation) 
	{
		tmp = insertAssemblyAfter(tmp, "lea rsp, [rsp+128]");
		block_record.push_back(tmp);
	}
	
	m_modifiedBlocks[blockid] = block_record;

	free(reg_temp); 
	free(reg_temp32);
	free(reg_temp16);
	free(reg_trace_map);
	free(reg_prev_id);
}

void Zax_t::insertForkServer(Instruction_t* p_entry)
{
	assert(p_entry);

	stringstream ss;
	ss << "0x" << hex << p_entry->getAddress()->getVirtualOffset();
	cout << "inserting fork server code at address: " << ss.str() << dec << endl;
	assert(p_entry->getAddress()->getVirtualOffset());

	if (p_entry->getFunction()) {
		cout << " function: " << p_entry->getFunction()->getName();
		cout << " ep instr: " << p_entry->getDisassembly() << endl;
	}
	cout << endl;

	// blacklist insertion point
	cout << "Blacklisting entry point: " << ss.str() << endl;
	m_blacklist.insert(ss.str());

	// insert the instrumentation
	auto tmp=p_entry;
	(void)insertAssemblyBefore(tmp, "lea rsp, [rsp-128]");
    	tmp=  insertAssemblyAfter(tmp," push rdi") ;
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
	tmp=  insertAssemblyAfter(tmp," lea rsp, [rsp+128]");
}

void Zax_t::insertForkServer(string p_forkServerEntry)
{
	assert(p_forkServerEntry.size() > 0);

	cout << "looking for fork server entry point: " << p_forkServerEntry << endl;

	if (std::isdigit(p_forkServerEntry[0]))
	{
		// find instruction to insert fork server based on address
		const auto voffset = (VirtualOffset_t) std::strtoul(p_forkServerEntry.c_str(), NULL, 16);
		auto instructions=find_if(getFileIR()->getInstructions().begin(), getFileIR()->getInstructions().end(), [&](const Instruction_t* i) {
				return i->getAddress()->getVirtualOffset()==voffset;
			});

		if (instructions==getFileIR()->getInstructions().end())
		{
			cerr << "Error: could not find address to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		insertForkServer(*instructions);
	}
	else
	{
		// find entry point of specified function to insert fork server
		auto entryfunc=find_if(getFileIR()->getFunctions().begin(), getFileIR()->getFunctions().end(), [&](const Function_t* f) {
				return f->getName()==p_forkServerEntry;
			});

		
		if(entryfunc==getFileIR()->getFunctions().end())
		{
			cerr << "Error: could not find function to insert fork server: " << p_forkServerEntry << endl;
			throw;
		}

		cout << "inserting fork server code at entry point of function: " << p_forkServerEntry << endl;
		auto entrypoint = (*entryfunc)->getEntryPoint();
		
		if (!entrypoint) 
		{
			cerr << "Could not find entry point for: " << p_forkServerEntry << endl;
			throw;
		}
		insertForkServer(entrypoint);
	}
}

void Zax_t::setupForkServer()
{
	if (m_fork_server_entry.size()>0)
	{
		// user has specified entry point
		insertForkServer(m_fork_server_entry);
	}
	else
	{
		// try to insert fork server at main
		const auto &all_funcs=getFileIR()->getFunctions();
		const auto main_func_it=find_if(all_funcs.begin(), all_funcs.end(), [&](const Function_t* f) { return f->getName()=="main";});
		if(main_func_it!=all_funcs.end())
		{
			insertForkServer("main"); 
		}

	}

	// it's ok not to have a fork server at all, e.g. libraries
}

void Zax_t::insertExitPoints()
{
	for (auto exitp : m_exitpoints)
	{
		if (std::isdigit(exitp[0]))
		{
			// find instruction to insert fork server based on address
			const auto voffset = (VirtualOffset_t) std::strtoul(exitp.c_str(), NULL, 16);
			auto instructions=find_if(getFileIR()->getInstructions().begin(), getFileIR()->getInstructions().end(), [&](const Instruction_t* i) {
					return i->getAddress()->getVirtualOffset()==voffset;
				});

			if (instructions==getFileIR()->getInstructions().end())
			{
				cerr << "Error: could not find address to insert exit point: " << exitp << endl;
				throw;
			}

			insertExitPoint(*instructions);
		}
		else
		{
			// find function by name
			auto func_iter=find_if(getFileIR()->getFunctions().begin(), getFileIR()->getFunctions().end(), [&](const Function_t* f) {
				return f->getName()==exitp;
			});

		
			if(func_iter==getFileIR()->getFunctions().end())
			{
				cerr << "Error: could not find function to insert exit points: " << exitp << endl;
				throw;
			}

			cout << "inserting exit code at return points of function: " << exitp << endl;
			for (auto i : (*func_iter)->getInstructions())
			{
				if (i->getBaseID() >= 0)
				{
					const auto d=DecodedInstruction_t::factory(i);

					// if it's a return instruction, add exit point
					if (d->isReturn())
					{
						insertExitPoint(i);
					}
				}
			}
		}
	}
}

static bool isConditionalBranch(const Instruction_t *i)
{
	const auto d=DecodedInstruction_t::factory(i);
	return (d->isConditionalBranch());
}

// blacklist functions:
//     - in blacklist
//     - that start with '.'
//     - that end with @plt
bool Zax_t::isBlacklisted(const Function_t *p_func) const
{
	return (p_func->getName()[0] == '.' || 
	        p_func->getName().find("@plt") != string::npos ||
	        m_blacklist.find(p_func->getName())!=m_blacklist.end());
}

bool Zax_t::isWhitelisted(const Function_t *p_func) const
{
	if (m_whitelist.size() == 0) return true;
	return (m_whitelist.find(p_func->getName())!=m_whitelist.end());
}

bool Zax_t::isBlacklisted(const Instruction_t *p_inst) const
{
	const auto vo = p_inst->getAddress()->getVirtualOffset();
	char tmp[1024];
	snprintf(tmp, 1000, "0x%x", (unsigned int)vo);
	return (m_blacklist.count(tmp) > 0 || isBlacklisted(p_inst->getFunction()));
}

bool Zax_t::isWhitelisted(const Instruction_t *p_inst) const
{
	if (m_whitelist.size() == 0) return true;

	const auto vo = p_inst->getAddress()->getVirtualOffset();
	char tmp[1024];
	snprintf(tmp, 1000, "0x%x", (unsigned int)vo);
	return (m_whitelist.count(tmp) > 0 || isWhitelisted(p_inst->getFunction()));
}

static void walkSuccessors(set<libIRDB::BasicBlock_t*> &p_visited_successors, libIRDB::BasicBlock_t *p_bb, libIRDB::BasicBlock_t *p_target)
{
	if (p_bb == NULL || p_target == NULL) 
		return;

	for (auto b : p_bb->GetSuccessors())
	{
		if (p_visited_successors.find(b) == p_visited_successors.end())
		{
//			cout << "bb anchored at " << b->getInstructions()[0]->getBaseID() << " is a successor of bb anchored at " << p_bb->getInstructions()[0]->getBaseID() << endl;
			p_visited_successors.insert(b);
			if (p_visited_successors.find(p_target) != p_visited_successors.end())
				return;
			walkSuccessors(p_visited_successors, b, p_target);
		}
	}
}

// @nb: move in BB class?
static bool hasBackEdge(libIRDB::BasicBlock_t *p_bb)
{
	assert(p_bb);
	if (p_bb->GetPredecessors().find(p_bb)!=p_bb->GetPredecessors().end()) 
		return true;
	if (p_bb->GetSuccessors().find(p_bb)!=p_bb->GetSuccessors().end()) 
		return true;
	if (p_bb->GetSuccessors().size() == 0) 
		return false;

	// walk successors recursively
	set<libIRDB::BasicBlock_t*> all_successors;

	cout << "Walk successors for bb anchored at: " << p_bb->GetInstructions()[0]->getBaseID() << endl;
	walkSuccessors(all_successors, p_bb, p_bb);
	if (all_successors.find(p_bb)!=all_successors.end())
		return true;

	return false;
}

void Zax_t::setup()
{
	if (m_forkserver_enabled)
		setupForkServer();
	else
		cout << "Fork server has been disabled" << endl;

	insertExitPoints();
}

void Zax_t::teardown()
{
	dumpAttributes();
	dumpMap();
}	

// in: control flow graph for a given function
// out: set of basic blocks to instrument
set<libIRDB::BasicBlock_t*> Zax_t::getBlocksToInstrument(libIRDB::ControlFlowGraph_t &cfg)
{
	static int bb_debug_id=-1;

	if (m_verbose)
		cout << cfg << endl;

	auto keepers = set<libIRDB::BasicBlock_t*>();

	for (auto &bb : cfg.GetBlocks())
	{
		assert(bb->GetInstructions().size() > 0);

		bb_debug_id++;

		// already marked as a keeper
		if (keepers.find(bb) != keepers.end())
			continue;
 
		// if whitelist specified, only allow instrumentation for functions/addresses in whitelist
		if (m_whitelist.size() > 0) 
		{
			if (!isWhitelisted(bb->GetInstructions()[0]))
			{
				continue;
			}
		}

		if (isBlacklisted(bb->GetInstructions()[0]))
			continue;

		// debugging support
		if (getenv("ZAFL_LIMIT_BEGIN"))
		{
			if (bb_debug_id < atoi(getenv("ZAFL_LIMIT_BEGIN")))
				continue;	
		}

		// debugging support
		if (getenv("ZAFL_LIMIT_END"))
		{
			if (bb_debug_id >= atoi(getenv("ZAFL_LIMIT_END"))) 
				continue;
		}

		// make sure we're not trying to instrument code we just inserted, e.g., fork server, added exit points
		if (bb->GetInstructions()[0]->getBaseID() < 0)
			continue;

		// push/jmp pair, don't bother instrumenting
		if (BB_isPushJmp(bb))
		{
			m_num_bb_skipped_pushjmp++;
			continue;
		}

		// padding nop, don't bother
		if (BB_isPaddingNop(bb))
		{
			m_num_bb_skipped_nop_padding++;
			continue;
		}

		// optimization:
		//    inner node: 1 predecessor and 1 successor
		//    
		//    predecessor has only 1 successor (namely this bb)
		//    bb has 1 predecessor 
		if (m_bb_graph_optimize)
		{
			auto point_to_self = false;
			if (bb->GetPredecessors().find(bb)!=bb->GetPredecessors().end()) {
				point_to_self = true;
			}
			if (bb->GetPredecessors().size()==1 && !point_to_self)
			{
				if (bb->GetSuccessors().size() == 1 && 
					(!bb->GetInstructions()[0]->getIndirectBranchTargetAddress()))
				{
					cout << "Skipping bb #" << dec << bb_debug_id << " because inner node with 1 predecessor and 1 successor" << endl;
					m_num_bb_skipped_innernode++;
					continue;
				}
					
				const auto pred = *(bb->GetPredecessors().begin());
				if (pred->GetSuccessors().size() == 1)
				{
					if (!bb->GetInstructions()[0]->getIndirectBranchTargetAddress())
					{
						cout << "Skipping bb #" << dec << bb_debug_id << " because not ibta, <1,*> and preds <*,1>" << endl;
						m_num_bb_skipped_onlychild++;
						continue;
					} 

					if (pred->GetIsExitBlock())
					{
						m_num_bb_skipped_onlychild++;
						cout << "Skipping bb #" << dec << bb_debug_id << " because ibta, <1,*> and preds(exit_block) <*,1>" << endl;
						continue;
					}
				}
			}

			// optimization conditional branch:
			//     elide conditional branch when no back edges
			if (bb->GetSuccessors().size() == 2 && isConditionalBranch(bb->GetInstructions()[bb->GetInstructions().size()-1]))
			{

				if (hasBackEdge(bb)) 
				{
					cout << "Keeping bb #" << dec << bb_debug_id << " conditional branch has back edge" << endl;
					m_num_bb_keep_cbranch_back_edge++;
					keepers.insert(bb);
					continue;
				}
				
				for (auto &s: bb->GetSuccessors())
				{
					if (s->GetIsExitBlock() || s->GetSuccessors().size()==0)
					{
						m_num_bb_keep_exit_block++;
						keepers.insert(s);
					}
				}

				cout << "Skipping bb #" << dec << bb_debug_id << " because conditional branch with 2 successors" << endl;
				m_num_bb_skipped_cbranch++;
				continue;
			}
		}

		keepers.insert(bb);
	}
	return keepers;
}

/*
 * Execute the transform.
 *
 * preconditions: the FileIR is read as from the IRDB. valid file listing functions to auto-initialize
 * postcondition: instructions added to auto-initialize stack for each specified function
 *
 */
int Zax_t::execute()
{
	setup();

	// for all functions
	//    build cfg and extract basic blocks
	//    for all basic blocks, figure out whether should be kept
	//    for all kept basic blocks
	//          add afl-compatible instrumentation
	
	struct BaseIDSorter
	{
	    bool operator()( const Function_t* lhs, const Function_t* rhs ) const {
		return lhs->getBaseID() < rhs->getBaseID();
	    }
	};
	set<Function_t*, BaseIDSorter> sortedFuncs(getFileIR()->getFunctions().begin(), getFileIR()->getFunctions().end());
	for_each( sortedFuncs.begin(), sortedFuncs.end(), [&](Function_t* f)
	{
		if (!f) return;
		// skip instrumentation for blacklisted functions 
		if (isBlacklisted(f)) return;
		// skip if function has no entry point
		if (!f->getEntryPoint())
			return;

		bool leafAnnotation = true;
		if (m_use_stars) 
		{
			leafAnnotation = hasLeafAnnotation(f, m_stars_analysis_engine.getAnnotations());
		}

		libIRDB::ControlFlowGraph_t cfg(f);

		const auto num_blocks_in_func = cfg.GetBlocks().size();
		m_num_bb += num_blocks_in_func;

		
		auto keepers = getBlocksToInstrument(cfg);
		struct BBSorter
		{
		    bool operator()( const libIRDB::BasicBlock_t* lhs, const libIRDB::BasicBlock_t* rhs ) const {
			return lhs->GetInstructions()[0]->getBaseID() < rhs->GetInstructions()[0]->getBaseID();
		    }
		};
		set<libIRDB::BasicBlock_t*, BBSorter> sortedBasicBlocks(keepers.begin(), keepers.end());
		for (auto &bb : sortedBasicBlocks)
		{
			auto collAflSingleton = false;
			// for collAfl-style instrumentation, we want #predecessors==1
			// if the basic block entry point is an IBTA, we don't know the #predecessors
			if (m_bb_graph_optimize && (bb->GetPredecessors().size() == 1)
						&& (!bb->GetInstructions()[0]->getIndirectBranchTargetAddress()))
			{
				collAflSingleton = true;
				m_num_style_collafl++;

			}

			afl_instrument_bb(bb->GetInstructions()[0], leafAnnotation, collAflSingleton);
		}


		m_num_bb_instrumented += keepers.size();
		m_num_bb_skipped += (num_blocks_in_func - keepers.size());

		if (m_verbose)
		{
			cout << "Post transformation CFG:" << endl;
			libIRDB::ControlFlowGraph_t post_cfg(f);	
			cout << post_cfg << endl;
		}

		cout << "Function " << f->getName() << ":  " << dec << keepers.size() << "/" << num_blocks_in_func << " basic blocks instrumented." << endl;
	});

	teardown();

	return 1;
}

void Zax_t::dumpAttributes()
{
	cout << "#ATTRIBUTE num_bb=" << dec << m_num_bb << endl;
	cout << "#ATTRIBUTE num_bb_instrumented=" << m_num_bb_instrumented << endl;
	cout << "#ATTRIBUTE num_bb_skipped=" << m_num_bb_skipped << endl;
	cout << "#ATTRIBUTE num_bb_skipped_pushjmp=" << m_num_bb_skipped_pushjmp << endl;
	cout << "#ATTRIBUTE num_bb_skipped_nop_padding=" << m_num_bb_skipped_nop_padding << endl;
	cout << "#ATTRIBUTE graph_optimize=" << boolalpha << m_bb_graph_optimize << endl;
	if (m_bb_graph_optimize)
	{
		cout << "#ATTRIBUTE num_bb_skipped_cond_branch=" << m_num_bb_skipped_cbranch << endl;
		cout << "#ATTRIBUTE num_bb_keep_cbranch_back_edge=" << m_num_bb_keep_cbranch_back_edge << endl;
		cout << "#ATTRIBUTE num_bb_keep_exit_block=" << m_num_bb_keep_exit_block << endl;
		cout << "#ATTRIBUTE num_style_collafl=" << m_num_style_collafl << endl;
		cout << "#ATTRIBUTE num_bb_skipped_onlychild=" << m_num_bb_skipped_onlychild << endl;
		cout << "#ATTRIBUTE num_bb_skipped_innernode=" << m_num_bb_skipped_innernode << endl;
	}
}

void Zax_t::dumpMap()
{
	// dump out modified basic block info
	getFileIR()->setBaseIDS();           // make sure instructions have IDs
	getFileIR()->assembleRegistry();     // make sure to assemble all instructions

	std::ofstream mapfile("zax.map");

	mapfile << "# BLOCK_ID  ID_EP:size  ID_OLDEP:size (ID_INSTRUMENTATION:size)*" << endl;
	for (auto &mb : m_modifiedBlocks)
	{
		const auto blockid = mb.first;
		mapfile << dec << blockid << " ";
		for (auto &entry : mb.second)
		{
			mapfile << hex << entry->getBaseID() << ":" << dec << entry->getDataBits().size() << " ";
		}
		mapfile << endl;
	}
}
