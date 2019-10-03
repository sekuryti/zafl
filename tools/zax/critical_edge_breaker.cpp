// @HEADER_LANG C++ 
// @HEADER_COMPONENT zafl
// @HEADER_BEGIN
/*
* Copyright (c) 2018-2019 Zephyr Software LLC
*
* This file may be used and modified for non-commercial purposes as long as
* all copyright, permission, and nonwarranty notices are preserved.
* Redistribution is prohibited without prior written consent from Zephyr
* Software.
*
* Please contact the authors for restrictions applying to commercial use.
*
* THIS SOURCE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
* MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*
* Author: Zephyr Software
* e-mail: jwd@zephyr-software.com
* URL   : http://www.zephyr-software.com/
*
* This software was developed with SBIR funding and is subject to SBIR Data Rights, 
* as detailed below.
*
* SBIR DATA RIGHTS
*
* Contract No. __FA8750-17-C-0295___________________________.
* Contractor Name __Zephyr Software LLC_____________________.
* Address __4826 Stony Point Rd, Barboursville, VA 22923____.
*
*/

// @HEADER_END

#include <irdb-cfg>
#include <irdb-transform>
#include <fstream>
#include "critical_edge_breaker.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

CriticalEdgeBreaker_t::CriticalEdgeBreaker_t(IRDB_SDK::FileIR_t *p_IR, set<string> p_blacklist, const bceStyle_t sty, const bool p_verbose) :
	m_IR(p_IR),
	m_blacklist(p_blacklist),
	m_verbose(p_verbose),
	m_extra_nodes(0),
	m_break_style(sty)
{
	// open and print a header line in ceb.map
	map_file.open("ceb.map");
	if(!map_file)
		throw invalid_argument("Cannot open ceb.map");
	map_file << "instID\tbroken\ttype" << endl;

	breakCriticalEdges();
}

unsigned CriticalEdgeBreaker_t::getNumberExtraNodes() const
{
	return m_extra_nodes;
}

// iterate over each function and break critical edges
void CriticalEdgeBreaker_t::breakCriticalEdges()
{
	auto is_blacklisted = [this](const Function_t* f) -> bool
		{
		  const auto fname = f->getName();
		  return (fname[0] == '.' || fname.find("@plt") != string::npos || m_blacklist.find(fname)!=m_blacklist.end());
		};

	for ( auto &f : m_IR->getFunctions() )
	{
		if (!f) continue;
		if (is_blacklisted(f)) continue;

		if (f->getEntryPoint())
			m_extra_nodes += breakCriticalEdges(f);
	}
}

//
// break critical edges by inserting an afl-instrumented dummy node
// if A --> B is a critical edge, break critical edge by adding C to yield:
//         A --> C --> B
//        
unsigned CriticalEdgeBreaker_t::breakCriticalEdges(Function_t* p_func)
{
	
	auto cfgp = ControlFlowGraph_t::factory(p_func);
	auto &cfg = *cfgp;

	auto ceap = CriticalEdges_t::factory(cfg, false);
	auto &cea = *ceap;

	const auto breakTargets  =  m_break_style == bceAll || m_break_style == bceTargets;
	const auto breakFallthru =  m_break_style == bceAll || m_break_style == bceFallthroughs;
	
	const auto critical_edges = cea.getAllCriticalEdges();
	auto num_critical_edges_instrumented = 0;

	cout << endl;
	cout << "Breaking critical edges for function: " << p_func->getName();
	cout << " - " << critical_edges.size() << " critical edges detected" << endl;

	if (m_verbose)
	{
		cout << "Original CFG: " << endl;
		cout << cfg << endl;
	}

	for (const auto &edge : critical_edges)
	{
		auto source_block = get<0>(edge);
		auto target_block = get<1>(edge);

		auto last_instruction_in_source_block = source_block->getInstructions()[source_block->getInstructions().size()-1];
		auto first_instruction_in_target_block = target_block->getInstructions()[0];


		if (source_block->endsInConditionalBranch())
		{
			// start a line in th emap
			map_file << hex << last_instruction_in_source_block ->getBaseID() << "\t";

			const auto func        = last_instruction_in_source_block->getFunction();
			const auto is_target   = last_instruction_in_source_block->getTarget() == first_instruction_in_target_block;
			const auto is_fallthru = last_instruction_in_source_block->getFallthrough() == first_instruction_in_target_block;

			if ( is_target && breakTargets)
			{
				auto jmp=m_IR->addNewInstruction(nullptr,func);	
				setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->setComment("break_critical_edge_jmp");

				last_instruction_in_source_block->setTarget(jmp);
				num_critical_edges_instrumented++;
				map_file << "true\ttarget";
			}
			else if (is_fallthru && breakFallthru)
			{
				auto jmp=m_IR->addNewInstruction(nullptr,func);	
				setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->setComment("break_critical_edge_fallthrough");

				last_instruction_in_source_block->setFallthrough(jmp);
				num_critical_edges_instrumented++;
				map_file << "true\tfallthrough";
			}
			else
			{
				if(is_target)
					map_file << "false\ttarget";
				else
					map_file << "false\tfallthrough";
			}
			map_file << endl;
		}
	}


	if (m_verbose)
	{
		m_IR->assembleRegistry();
		cout << "Number critical edge instrumented: " << num_critical_edges_instrumented << endl;
		auto post_cfgp = ControlFlowGraph_t::factory(p_func);
		auto &post_cfg = *post_cfgp;
		cout << "Post CFG: " << endl;
		cout << post_cfg << endl;
	}
	return num_critical_edges_instrumented;
}


