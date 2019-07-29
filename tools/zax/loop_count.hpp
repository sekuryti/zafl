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

#ifndef _LIB_ZEDGE_H
#define _LIB_ZEDGE_H

#include <irdb-core>
#include <irdb-transform>
#include <irdb-deep>

namespace ZedgeNS
{
	using namespace std;
	using namespace IRDB_SDK;

	class Zedge_t : protected Transform_t
	{
		public:
			Zedge_t(FileIR_t *p_variantIR, const string& p_loopCountBuckets);
			bool execute();

		private:
			set<uint32_t> m_loopCountBuckets = {};
	};

}
#endif
