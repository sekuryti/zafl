// @HEADER_LANG C++ 
// @HEADER_COMPONENT zafl
// @HEADER_BEGIN
/*
Copyright (c) 2018-2021 Zephyr Software LLC

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    (1) Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer. 

    (2) Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.  
    
    (3) The name of the author may not be used to
    endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE. 
*/

// @HEADER_END

#include <fstream>
#include <iomanip>
#include <irdb-cfg>
#include <irdb-transform>
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
	map_file << "instID\tbroken\ttype\tNewInsnID" << endl;

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


	// emit broken edges into the map after updating the base IDs
	m_IR->setBaseIDS();

	const auto emit_line=[&](Instruction_t* from, Instruction_t* to, const string& type) -> void
		{
			// start a line in th emap
			map_file << hex << from ->getBaseID() << "\t";
			map_file << "true\t" << type << "\t" << hex << setw(8) << to->getBaseID();
			map_file << endl;
		};

	for(const auto insn : broken_fallthroughs)
		emit_line(insn,insn->getFallthrough(), "fall");

	for(const auto insn : broken_targets)
		emit_line(insn,insn->getTarget(), "targ");
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

		auto last_instruction_in_source_block  = source_block->getInstructions()[source_block->getInstructions().size()-1];
		auto first_instruction_in_target_block = target_block->getInstructions()[0];


		if (source_block->endsInConditionalBranch())
		{

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
				broken_targets.insert(last_instruction_in_source_block);
			}
			else if (is_fallthru && breakFallthru)
			{
				auto jmp=m_IR->addNewInstruction(nullptr,func);	
				setInstructionAssembly(m_IR, jmp, "jmp 0", nullptr, first_instruction_in_target_block);
				jmp->setComment("break_critical_edge_fallthrough");
				last_instruction_in_source_block->setFallthrough(jmp);

				num_critical_edges_instrumented++;
				broken_fallthroughs.insert(last_instruction_in_source_block);
			}
			else
			{
				map_file << hex << last_instruction_in_source_block->getBaseID() << "\t";
				if(is_target)
					map_file << "false\ttarg\tffffffff";
				else
					map_file << "false\tfall\tffffffff";
				map_file << endl;
			}
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


