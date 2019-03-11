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

#include <getopt.h>

#include "laf.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Laf;

void usage(char* name)
{
	cerr<<"Usage: "<<name<<" <variant_id>\n";
	cerr<<"\t[--verbose | -v]                       Verbose mode                  "<<endl;
	cerr<<"\t[--enable-split-compare | -c]          Enable split compare          "<<endl;
	cerr<<"\t[--disable-split-compare | -c]         Disable split compare          "<<endl;
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
	auto split_compare = true;
	auto trace_div = true;

	// Parse some options for the transform
	static struct option long_options[] = {
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},
		{"usage", no_argument, 0, '?'},
		{"enable-split-compare", no_argument, 0, 'c'},
		{"disable-split-compare", no_argument, 0, 'C'},
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
			split_compare=true;
			break;
		case 'C':
			split_compare=false;
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
			laf.setSplitCompare(split_compare);
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
		catch (DatabaseError_t pnide)
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

