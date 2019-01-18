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

#include <libIRDB-cfg.hpp>
#include <Rewrite_Utility.hpp>

#include "critical_edge_breaker.hpp"

using namespace std;
using namespace libIRDB;
using namespace Zafl;
using namespace IRDBUtility;

CriticalEdgeBreaker_t::CriticalEdgeBreaker_t(libIRDB::FileIR_t *p_IR, const bool p_verbose) :
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
	for ( auto &f : m_IR->GetFunctions() )
	{
		if (f && f->GetEntryPoint())
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
	ControlFlowGraph_t cfg(p_func);
	const CriticalEdgeAnalyzer_t cea(cfg, false);
	const auto critical_edges = cea.GetAllCriticalEdges();
	auto num_critical_edges_instrumented = 0;

	cout << endl;
	cout << "Breaking critical edges for function: " << p_func->GetName();
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

		auto last_instruction_in_source_block = source_block->GetInstructions()[source_block->GetInstructions().size()-1];
		auto first_instruction_in_target_block = target_block->GetInstructions()[0];

		if (source_block->EndsInConditionalBranch())
		{
			const auto fileID = last_instruction_in_source_block->GetAddress()->GetFileID();
			const auto func = last_instruction_in_source_block->GetFunction();

			if (last_instruction_in_source_block->GetTarget() == first_instruction_in_target_block)
			{
				auto jmp = IRDBUtility::allocateNewInstruction(m_IR, fileID, func);
				IRDBUtility::setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->SetComment("break_critical_edge_jmp");

				last_instruction_in_source_block->SetTarget(jmp);
				num_critical_edges_instrumented++;
			}
			else if (last_instruction_in_source_block->GetFallthrough() == first_instruction_in_target_block)
			{
				auto jmp = IRDBUtility::allocateNewInstruction(m_IR, fileID, func);
				IRDBUtility::setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->SetComment("break_critical_edge_fallthrough");

				last_instruction_in_source_block->SetFallthrough(jmp);
				num_critical_edges_instrumented++;
			}
		}
	}

	cout << "Number critical edge instrumented: " << num_critical_edges_instrumented << endl;

	if (m_verbose)
	{
		ControlFlowGraph_t post_cfg(p_func);
		m_IR->AssembleRegistry();
		cout << "Post CFG: " << endl;
		cout << post_cfg << endl;
	}
	return num_critical_edges_instrumented;
}


