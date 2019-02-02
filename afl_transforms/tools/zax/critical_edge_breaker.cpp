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

#include <irdb-cfg>
#include <irdb-transform>

#include "critical_edge_breaker.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

CriticalEdgeBreaker_t::CriticalEdgeBreaker_t(IRDB_SDK::FileIR_t *p_IR, const bool p_verbose) :
	m_IR(p_IR),
	m_verbose(p_verbose),
	m_extra_nodes(0)
{
	breakCriticalEdges();
}

unsigned CriticalEdgeBreaker_t::getNumberExtraNodes() const
{
	return m_extra_nodes;
}

// iterate over each function and break critical edges
void CriticalEdgeBreaker_t::breakCriticalEdges()
{
	for ( auto &f : m_IR->getFunctions() )
	{
		if (f && f->getEntryPoint())
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
//	ControlFlowGraph_t cfg(p_func);
	auto cfgp = ControlFlowGraph_t::factory(p_func);
	auto &cfg = *cfgp;

//	const CriticalEdgeAnalyzer_t cea(cfg, false);
	auto ceap = CriticalEdges_t::factory(cfg, false);
	auto &cea = *ceap;
	
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
			// const auto fileID = last_instruction_in_source_block->getAddress()->getFileID();
			const auto func = last_instruction_in_source_block->getFunction();

			if (last_instruction_in_source_block->getTarget() == first_instruction_in_target_block)
			{
				//auto jmp = IRDBUtility::allocateNewInstruction(m_IR, fileID, func);
				auto jmp=m_IR->addNewInstruction(nullptr,func);	
				setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->setComment("break_critical_edge_jmp");

				last_instruction_in_source_block->setTarget(jmp);
				num_critical_edges_instrumented++;
			}
			else if (last_instruction_in_source_block->getFallthrough() == first_instruction_in_target_block)
			{
				// auto jmp = IRDBUtility::allocateNewInstruction(m_IR, fileID, func);
				auto jmp=m_IR->addNewInstruction(nullptr,func);	
				setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->setComment("break_critical_edge_fallthrough");

				last_instruction_in_source_block->setFallthrough(jmp);
				num_critical_edges_instrumented++;
			}
		}
	}


	if (m_verbose)
	{
		cout << "Number critical edge instrumented: " << num_critical_edges_instrumented << endl;
//		ControlFlowGraph_t post_cfg(p_func);
		auto post_cfgp = ControlFlowGraph_t::factory(p_func);
		auto &post_cfg = *post_cfgp;
		m_IR->assembleRegistry();
		cout << "Post CFG: " << endl;
		cout << post_cfg << endl;
	}
	return num_critical_edges_instrumented;
}


