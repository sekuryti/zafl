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
#include <irdb-cfg>
#include <irdb-transform>
#include <irdb-elfdep>

#include "zax.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;
using namespace MEDS_Annotation;

Zax_t::Zax_t(IRDB_SDK::pqxxDB_t &p_dbinterface, IRDB_SDK::FileIR_t *p_variantIR, string p_forkServerEntryPoint, set<string> p_exitPoints, bool p_use_stars, bool p_autozafl) : ZaxBase_t(p_dbinterface, p_variantIR, p_forkServerEntryPoint, p_exitPoints, p_use_stars, p_autozafl)
{
}

/*
 * Return random block id
 * Try to avoid duplicate ids
 */
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
void Zax_t::afl_instrument_bb(Instruction_t *p_inst, const bool p_honorRedZone, const bool p_collafl_optimization)
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
	auto block_record=BBRecord_t();

	const auto trace_map_fixed_addr       = getenv("ZAFL_TRACE_MAP_FIXED_ADDRESS");
	const auto do_fixed_addr_optimization = (trace_map_fixed_addr!=nullptr);

	// don't try to reserve the trace_map reg if we aren't using it.
	if(do_fixed_addr_optimization)  
		save_trace_map=false;

	block_record.push_back(p_inst);

	// If we are using stars, try to assign rax, rcx, and rdx to their 
	// most desireable position in the instrumentation.
	if (m_use_stars) 
	{
		auto regset = get_dead_regs(p_inst, m_stars_analysis_engine.getAnnotations());
		live_flags = regset.find(IRDB_SDK::rn_EFLAGS)==regset.end();
		const auto allowed_regs = RegisterSet_t({rn_RAX, rn_RBX, rn_RCX, rn_RDX, rn_R8, rn_R9, rn_R10, rn_R11, rn_R12, rn_R13, rn_R14, rn_R15});

		auto free_regs = get_free_regs(regset, allowed_regs);

		for (auto r : regset)
		{
			if (r == rn_RAX)
			{
				reg_temp = strdup("rax"); reg_temp32 = strdup("eax"); reg_temp16 = strdup("ax");
				save_temp = false;
				free_regs.erase(r);
			}
			else if (r == rn_RCX && save_trace_map)
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

		// if we failed to do the assignment, check for any other register to fill the assignment.
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

	// In the event we couldn't find a free register, or we aren't using stars
	// to identify free registers, use the default registers.  We will save and
	// restore these later.
	if (!reg_temp)      reg_temp      = strdup("rax");
	if (!reg_temp32)    reg_temp32    = strdup("eax");
	if (!reg_temp16)    reg_temp16    = strdup("ax");
	if (!reg_trace_map) reg_trace_map = strdup("rcx");
	if (!reg_prev_id)   reg_prev_id   = strdup("rdx");

	if (m_verbose)
	{
		cout << "save_temp: " << save_temp << " save_trace_map: " << save_trace_map << " save_prev_id: " << save_prev_id << " live_flags: " << live_flags << endl;
		cout << "reg_temp: " << reg_temp << " " << reg_temp32 << " " << reg_temp16 
			<< " reg_trace_map: " << reg_trace_map
			<< " reg_prev_id: " << reg_prev_id << endl;
	}

	// warning: first instrumentation must use insertAssemblyBefore
	// others use insertAssemblyAfter.
	// we declare a macro-like lambda function to do the lifting for us.
	auto inserted_before = false;
	auto tmp = p_inst;
	const auto do_insert=[&](const string& insn_str) -> void
		{
			if (inserted_before)
			{
				tmp = insertAssemblyAfter(tmp, insn_str);
				block_record.push_back(tmp);
			}
			else
			{
				const auto orig = insertAssemblyBefore(tmp, insn_str);
				inserted_before = true;
				block_record.push_back(orig);
			}
		};

	// get some IDs which we can use as to generate custom labels
	const auto blockid = get_blockid();
	const auto labelid = get_labelid(); 

	if (m_verbose)
	{
		cout << "labelid: " << labelid << " baseid: " << p_inst->getBaseID() << " address: 0x" 
		     << hex << p_inst->getAddress()->getVirtualOffset() << dec << " instruction: " 
		     << p_inst->getDisassembly();
	}

	// Emit the instrumentation register-saving phase.
	// Omit saving any registers we don't need to save because we
	// were able to locate a free register.
	if (p_honorRedZone)  do_insert("lea rsp, [rsp-128]");
	if (save_temp)            do_insert("push rax");
	if (save_trace_map)       do_insert("push rcx");
	if (save_prev_id)         do_insert("push rdx");
	if (live_flags)           do_insert("pushf");

	const auto live_flags_str = live_flags ? "live" : "dead"; 
	if (m_verbose) cout << "   flags are "  << live_flags_str << endl;


/*
   0:   mov    rdx,QWORD PTR [rip+0x0]        # load previous block id.
   7:   mov    rcx,QWORD PTR [rip+0x0]        # load trace map address.

<no collafl optimization>
   e:   	movzx  eax,WORD PTR [rdx]     # hash prev-block-id with this-block id (0x1234)
  11:   	xor    ax,0x1234                              
  15:   	movzx  eax,ax                                

<collafl-style optimization>
<noaddr>	mov    eax, <blockid>	     # faster hash -- ignore previous id.

  18:   add    rax,QWORD PTR [rcx]           # generate address of trace map to bump 
  ib:   add    BYTE PTR [rax],0x1            # bump the entr
  1e:   mov    eax,0x91a                     # write this block ID back to the prev-id variable.  
  23:   mov    WORD PTR [rdx],ax       
*/	

	// load the previous block ID.
	//   0:   mov    rdx,QWORD PTR [rip+0x0]        # 7 <f+0x7>
// FIXME:  Why are we doing this if we aren't bothering to hash the previous block ID?
	sprintf(buf, "P%d: mov  %s, QWORD [rel P%d]", labelid, reg_prev_id, labelid); // rdx
	do_insert(buf);
	create_got_reloc(getFileIR(), m_prev_id, tmp);

	// if we are using a variable address trace map, generate the address.
	if(!do_fixed_addr_optimization)
	{
		//   7:   mov    rcx,QWORD PTR [rip+0x0]        # e <f+0xe>
		sprintf(buf, "T%d: mov  %s, QWORD [rel T%d]", labelid, reg_trace_map, labelid); 
		do_insert(buf);
		create_got_reloc(getFileIR(), m_trace_map, tmp);
	}

	// do the calculation to has the previouus block ID with this block ID
	// in the faster or slower fashion depending on the requested technique.
	if (!p_collafl_optimization)
	{
		//   e:   movzx  eax,WORD PTR [rdx]                      
		sprintf(buf,"movzx  %s,WORD [%s]", reg_temp32, reg_prev_id);
		do_insert(buf);

		//  11:   xor    ax,0x1234                              
		sprintf(buf, "xor   %s,0x%x", reg_temp16, blockid);
		do_insert(buf);
	
		//  15:   movzx  eax,ax                                
		sprintf(buf,"movzx  %s,%s", reg_temp32, reg_temp16);
		do_insert(buf);
	}
	else
	{
		// <noaddr> mov    rax, <blockid>
		sprintf(buf,"mov   %s,0x%x", reg_temp, blockid);
		do_insert(buf);
	
	}

	// write into the trace map.
	if(do_fixed_addr_optimization)
	{
		// do it the fast way with the fixed-adresss trace map
		//  1b:   80 00 01                add    BYTE PTR [rax],0x1                  
		sprintf(buf,"add    BYTE [%s + 0x%lx],0x1", reg_temp, strtoul(trace_map_fixed_addr,nullptr,0) );
		do_insert(buf);
	}
	else
	{
		// do it the slow way with the variable-adresss trace map
		//  18: add    rax,QWORD PTR [rcx]                  
		sprintf(buf,"add    %s,QWORD [%s]", reg_temp, reg_trace_map);
		do_insert(buf);

		//  1b: add    BYTE PTR [rax],0x1                  
		sprintf(buf,"add    BYTE [%s],0x1", reg_temp);
		do_insert(buf);
	}

	// write out block id into zafl_prev_id for the next instrumentation.
// FIXME:  Why are we doing this if we aren't bothering to hash the previous block ID?
	//  1e:   mov    eax,0x91a                          
	sprintf(buf, "mov   %s, 0x%x", reg_temp32, blockid >> 1);
	do_insert(buf);

	//  23:   mov    WORD PTR [rdx],ax       
	sprintf(buf, "mov    WORD [%s], %s", reg_prev_id, reg_temp16);
	do_insert(buf);

	// finally, restore any flags/registers so that the program can execute.
	if (live_flags)          do_insert("popf");
	if (save_prev_id)        do_insert("pop rdx");
	if (save_trace_map)      do_insert("pop rcx");
	if (save_temp)           do_insert("pop rax");
	if (p_honorRedZone) do_insert("lea rsp, [rsp+128]");
	
	m_modifiedBlocks[blockid] = block_record;


// FIXME: use strings instead of strdup and buffers everywhere.  
// there is no chance the mallocs/frees in this function match properly.
	free(reg_temp); 
	free(reg_temp32);
	free(reg_temp16);
	free(reg_trace_map);
	free(reg_prev_id);
}

