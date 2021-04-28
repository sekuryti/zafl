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

#include <getopt.h>

#include "laf.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Laf;

void usage(char* name)
{
	cerr<<"Usage: "<<name<<" <variant_id>\n";
	cerr<<"\t[--verbose | -v]                       Verbose mode                  "<<endl;
	cerr<<"\t[--enable-trace-compare | -c]          Enable trace compare          "<<endl;
	cerr<<"\t[--disable-trace-compare | -c]         Disable trace compare          "<<endl;
	cerr<<"\t[--enable-trace-div | -d]              Enable trace div          "<<endl;
	cerr<<"\t[--disable-trace-div | -D]             Disable trace div          "<<endl;
	cerr<<"[--help,--usage,-?,-h]                   Display this message           "<<endl;
}

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		usage(argv[0]);
		exit(1);
	}

	string programName(argv[0]);
	auto variantID = atoi(argv[1]);
	auto verbose=false;
	auto trace_compare = true;
	auto trace_div = true;

	// Parse some options for the transform
	static struct option long_options[] = {
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},
		{"usage", no_argument, 0, '?'},
		{"enable-trace-compare", no_argument, 0, 'c'},
		{"disable-trace-compare", no_argument, 0, 'C'},
		{"enable-trace-div", no_argument, 0, 'd'},
		{"disable-trace-div", no_argument, 0, 'D'},
		{0,0,0,0}
	};

	const char* short_opts="v?hcCdD";
	while(true)
	{
		int index = 0;
		int c = getopt_long(argc, argv, short_opts, long_options, &index);
		if(c == -1)
			break;
		switch(c)
		{
		case 'v':
			verbose=true;
			break;
		case '?':
		case 'h':
			usage(argv[0]);
			exit(1);
			break;
		case 'c':
			trace_compare=true;
			break;
		case 'C':
			trace_compare=false;
			break;
		case 'd':
			trace_div=true;
			break;
		case 'D':
			trace_div=false;
			break;
		default:
			break;
		}
	}


	/* setup the interface to the sql server */
	auto pqxx_interface=pqxxDB_t::factory();
	BaseObj_t::setInterface(pqxx_interface.get());

	auto pidp=VariantID_t::factory(variantID);
	assert(pidp->isRegistered()==true);

	bool one_success = false;
	for(set<File_t*>::iterator it=pidp->getFiles().begin();
	        it!=pidp->getFiles().end();
	        ++it)
	{
		File_t* this_file = *it;
		auto firp = FileIR_t::factory(pidp.get(), this_file);

		cout<<"Transforming "<<this_file->getURL()<<endl;

		assert(firp && pidp);

		try
		{
			Laf_t laf(*pqxx_interface, firp.get(), verbose);
			laf.setTraceCompare(trace_compare);
			laf.setTraceDiv(trace_div);

			int success=laf.execute();

			if (success)
			{
				cout<<"Writing changes for "<<this_file->getURL()<<endl;
				one_success = true;
				firp->writeToDB();
			}
			else
			{
				cout<<"Skipping (no changes) "<<this_file->getURL()<<endl;
			}
		}
		catch (const DatabaseError_t &pnide)
		{
			cerr << programName << ": Unexpected database error: " << pnide << "file url: " << this_file->getURL() << endl;
			exit(1);
		}
		catch (...)
		{
			cerr << programName << ": Unexpected error file url: " << this_file->getURL() << endl;
			exit(1);
		}
	} // end file iterator

	// if any transforms for any files succeeded, we commit
	if (one_success)
	{
		cout<<"Commiting changes...\n";
		pqxx_interface->commit();
	}

	return 0;
}

