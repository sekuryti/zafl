/*
   Copyright 2017-2019 University of Virginia

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <assert.h>
#include "loop_count.hpp"

using namespace ZedgeNS;

static RegisterIDSet_t getDeadRegs(const DeadRegisterMap_t& dead_registers, Instruction_t* insn) 
{
        const auto it = dead_registers.find(insn);
        if(it != dead_registers.end())
                return it->second;
        return RegisterIDSet_t();
}

static void create_scoop_reloc(FileIR_t* fir, pair<DataScoop_t*,int> wrt, Instruction_t* i)
{
        (void)fir->addNewRelocation(i, wrt.second, "pcrel", wrt.first);
}



Zedge_t::Zedge_t(FileIR_t *p_variantIR, const string& p_buckets)
	: 
	Transform_t(p_variantIR)
{
	stringstream ss(p_buckets);

	auto bucket = 0u;
	auto comma = ',';
	while( ss >> bucket )
	{
		m_loopCountBuckets.insert(bucket);
		if(! (ss >> comma))
			break;
	}

	for(auto bucket : m_loopCountBuckets)
	{
		cout <<"Loop count bucket has: "<< dec << bucket << endl;
	}
}

bool Zedge_t::execute()
{
	// declare and construct a deep analysis engine
	const auto de             = DeepAnalysis_t::factory(getFileIR());
	const auto dead_registers = de -> getDeadRegisters();

	static auto label_counter=0;

	auto new_blk_count      = 0u;
	auto loops_instrumented = 0u;
	auto flag_save_points   = 0u;


	for(auto f : getFileIR()->getFunctions())
	{
		const auto loop_nest = de->getLoops(f);
		for(auto loop : loop_nest->getAllLoops())
		{
			const auto header = loop -> getHeader(); 
			auto loop_start = header->getInstructions()[0];

			// do this here, before we insertBefore on loop_start
			const auto dead_regs  = getDeadRegs(*dead_registers, loop_start);

			cout << "Loop header: " << hex << loop_start->getBaseID() << ":" << loop_start->getDisassembly() << endl;

			// create the loop counter
			auto new_sa = getFileIR()->addNewAddress(getFileIR()->getFile()->getBaseID(), 0);
			auto new_ea = getFileIR()->addNewAddress(getFileIR()->getFile()->getBaseID(), 3);
			auto new_na = string() + "zedge_counter" + to_string(loop_start->getBaseID());
			auto new_co = string(4, '\0');
			auto new_sc = getFileIR()->addNewDataScoop(new_na, new_sa, new_ea, nullptr, 0x3, false, new_co);

			// for now, assume the flags are dead, they probably will be.  We will save/restore them later.

			// record the last instruction we inserted.
			auto tmp = loop_start;

			// insert code to bump the loop counter at the start of the loop
			auto inc_cnt_str = to_string(label_counter++);
			auto inc_str     = string() + "L"+ inc_cnt_str +": inc dword [rel L"+inc_cnt_str + "]";
			auto inc         = loop_start; 
			loop_start = insertAssemblyBefore(tmp, inc_str);
			create_scoop_reloc(getFileIR(), {new_sc,0}, inc);
				
			const auto &bucket_thresholds = m_loopCountBuckets;

			for(auto bucket_threshold : bucket_thresholds)
			{
				// the block to jump to if the comparison for this bucket is true.
				auto new_hlt_blk = addNewAssembly("jmp 0");
				new_hlt_blk->setTarget(loop_start);

				// the actual comparison of the counter to the bucket thresholds
				auto loop_cnt_str = to_string(label_counter++);
				auto cmp_str      = string() + "L"+ loop_cnt_str +": cmp dword [rel L"+loop_cnt_str + "], "+to_string(bucket_threshold);
				auto new_cmp      = tmp = insertAssemblyAfter(tmp,cmp_str);
				create_scoop_reloc(getFileIR(), {new_sc,0}, new_cmp);
				                    tmp = insertAssemblyAfter(tmp, "jg  0", new_hlt_blk);
			}
			new_blk_count      += 2 * bucket_thresholds.size();
			loops_instrumented += 1;

			// now, go back and save/restore context around the instrumentation as necessary.
                        const auto live_flags = dead_regs.find(IRDB_SDK::rn_EFLAGS)==dead_regs.end();
			if(live_flags)
			{
				flag_save_points += 1;
				(void)insertAssemblyBefore(inc,        "pushf");
				(void)insertAssemblyBefore(loop_start, "popf") ;
			}

		}
	}

	cout << dec; 
	cout << "#ATTRIBUTE zax::loop_count_total_blocks_inserted="    << new_blk_count      << endl;
	cout << "#ATTRIBUTE zax::loop_count_total_loops_instrumented=" << loops_instrumented << endl;
	cout << "#ATTRIBUTE zax::loop_count_total_flags_saves="        << flag_save_points  << endl;

	// success!
	return true;
}


