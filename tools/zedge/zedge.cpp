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
#include "zedge.hpp"

using namespace ZedgeNS;

Zedge_t::Zedge_t(FileIR_t *p_variantIR)
	: 
	Transform_t(p_variantIR) 
{
	// no other setup needed	
}

bool Zedge_t::execute()
{
	// declare and construct a deep analysis engine
	const auto de = DeepAnalysis_t::factory(getFileIR());

	for(auto f : getFileIR()->getFunctions())
	{
		const auto loop_nest = de->getLoops(f);
		for(auto loop : loop_nest->getAllLoops())
		{
			const auto header = loop -> getHeader(); 
			auto loop_start = header->getInstructions()[0];

			cout << "Loop header: " << hex << loop_start->getBaseID() << ":" << loop_start->getDisassembly() << endl;
			
			// instrument loop_start here.
		}
	}

	// success!
	return true;
}


