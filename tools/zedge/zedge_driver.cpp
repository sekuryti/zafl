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

#include <stdlib.h>
#include <fstream>
#include <irdb-core>
#include <libgen.h>
#include "zedge.hpp"

using namespace ZedgeNS;

//
// zedge is a Thanos-enabled transform.  Thanos-enabled transforms must implement the TransfromStep_t abstract class.
// See the IRDB SDK for additional details.
// 
// Since this class is simple and shouldn't be used elsewhere, we just implement the class in the .cpp file for conviencence
//
// Note: public inheritence here is required for Thanos integration
//
class ZedgeDriver_t : public TransformStep_t
{
	public:
		// 
		// required override: how to parse your options
		//
		int parseArgs(const vector<string> step_args) override
		{
			// no arguments to parse.
			return 0; // success (bash-style 0=success, 1=warnings, 2=errors)	
		}

		// 
		// required override: how to achieve the actual transform
		//
		int executeStep() override
		{
			// record the URL from the main file for log output later
			auto url=getMainFile()->getURL();

			// try to load and transform the file's IR.
			try
			{
				// load the fileIR (or, get the handle to an already loaded IR)
				auto firp = getMainFileIR();

				// create a transform object and execute a transform
				auto success = Zedge_t(firp).execute();

				// check for success
				if (success)
				{
					cout << "Success! Thanos will write back changes for " <<  url << endl;
					return 0; // success (bash-style 0=success, 1=warnings, 2=errors)	
				}

				// failure
				cout << "Failure!  Thanos will report error to user for " << url << endl;
				return 2; // error
			}
			catch (DatabaseError_t db_err)
			{
				cerr << program_name << ": Unexpected database error: " << db_err << "file url: " << url << endl;
				return 2; // error
			}
			catch (...)
			{
				cerr << program_name << ": Unexpected error file url: " << url << endl;
				return 2; // error
			}
			assert(0); // unreachable
		}

		// 
		// required override:  report the step name
		//
		string getStepName(void) const override
		{
			return program_name;
		}

	private:
	// data
		const string program_name = string("zedge");	// constant program name

	// methods

		//
		// optional:  print using info for this transform.
		// This transform takes no parameters.
		//
		void usage(const string& p_name)
		{
			cerr << "Usage: " << p_name << endl;
		}
};


//
// Required interface: a factory for creating the interface object for this transform.
//
extern "C"
shared_ptr<TransformStep_t> getTransformStep(void)
{
	return shared_ptr<TransformStep_t>(new ZedgeDriver_t());
}

