/***************************************************************************
 * Copyright (c)  2018  Zephyr Software LLC. All rights reserved.
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

#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "zafl.hpp"

using namespace std;
using namespace libIRDB;
using namespace Zafl;

static void usage(char* name)
{
	cerr<<"Usage: "<<name<<" <variant_id>\n";
	cerr<<"\t[--verbose | -v]                               Verbose mode                  "<<endl;
	cerr<<"\t[--help,--usage,-?,-h]                         Display this message          "<<endl;
	cerr<<"\t[--stars|-s]                                   Enable STARS optimizations    "<<endl;
	cerr<<"\t[--entrypoint|-e {<funcName>|<hex_address>}]   Specify where to insert fork server (defaults to main if found)"<<endl;
	cerr<<"\t[--exitpoint|-E {<funcName>|<hex_address>}]    Specify where to insert exit"<<endl;
	cerr<<"\t[--whitelist|-w whitelistFile]                 Specify list of functions/addresses to instrument"<<endl;
	cerr<<"\t[--blacklist|-b blacklistFile]                 Specify list of functions/addresses to omit"<<endl;
	cerr<<"\t[--enable-bb-graph-optimization|-g]            Elide instrumentation if basic block has 1 successor"<<endl;
	cerr<<"\t[--disable-bb-graph-optimization|-G]           Elide instrumentation if basic block has 1 successor"<<endl;
	cerr<<"\t[--autozafl|-a]                                Auto-initialize fork server (incompatible with --entrypoint)"<<endl;
}

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		usage(argv[0]);
		exit(1);
	}

	string programName(argv[0]);
	auto entry_fork_server = string();
	auto variantID = atoi(argv[1]);
	auto verbose=false;
	auto use_stars=false;
	auto autozafl=false;
	auto whitelistFile=string();
	auto blacklistFile=string();
	auto bb_graph_optimize=false;
	set<string> exitpoints;

	srand(getpid()+time(NULL));

	// Parse some options for the transform
	static struct option long_options[] = {
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'},
		{"usage", no_argument, 0, '?'},
		{"stars", no_argument, 0, 's'},
		{"entrypoint", required_argument, 0, 'e'},
		{"exitpoint", required_argument, 0, 'E'},
		{"whitelist", required_argument, 0, 'w'},
		{"blacklist", required_argument, 0, 'b'},
		{"autozafl", no_argument, 0, 'a'},
		{"enable-bb-graph-optimization", no_argument, 0, 'g'},
		{"disable-bb-graph-optimization", no_argument, 0, 'G'},
		{0,0,0,0}
	};
	const char* short_opts="e:E:w:sv?hagG";

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
			exit(0);
			break;
		case 's':
			use_stars=true;
			cout << "STARS optimization enabled" << endl;
			break;
		case 'e':
			entry_fork_server = optarg;
			break;
		case 'E':
			exitpoints.insert(optarg);
			break;
		case 'w':
			whitelistFile=optarg;
			break;
		case 'b':
			blacklistFile=optarg;
			break;
		case 'a':
			autozafl=true;
			break;
		case 'g':
			bb_graph_optimize=true;;
			break;
		case 'G':
			bb_graph_optimize=false;
			break;
		default:
			break;
		}
	}

	if (entry_fork_server.size() > 0 && autozafl)
	{
		cerr << "--entrypoint and --autozafl are incompatible options" << endl;
		usage(argv[0]);
		exit(1);
	}

	VariantID_t *pidp=NULL;

	/* setup the interface to the sql server */
	pqxxDB_t pqxx_interface;
	BaseObj_t::SetInterface(&pqxx_interface);

	pidp=new VariantID_t(variantID);
	assert(pidp->IsRegistered()==true);

	bool one_success = false;
	for(set<File_t*>::iterator it=pidp->GetFiles().begin();
	        it!=pidp->GetFiles().end();
	        ++it)
	{
		File_t* this_file = *it;
		FileIR_t *firp = new FileIR_t(*pidp, this_file);

		cout<<"Transforming "<<this_file->GetURL()<<endl;

		assert(firp && pidp);

		try
		{
			Zafl_t zafl_transform(pqxx_interface, firp, entry_fork_server, exitpoints, use_stars, autozafl, verbose);
			if (whitelistFile.size()>0)
				zafl_transform.setWhitelist(whitelistFile);
			if (blacklistFile.size()>0)
				zafl_transform.setBlacklist(blacklistFile);
			zafl_transform.setBasicBlockOptimization(bb_graph_optimize);

			int success=zafl_transform.execute();

			if (success)
			{
				cout<<"Writing changes for "<<this_file->GetURL()<<endl;
				one_success = true;
				firp->WriteToDB();
				delete firp;
			}
			else
			{
				cout<<"Skipping (no changes) "<<this_file->GetURL()<<endl;
			}
		}
		catch (const DatabaseError_t pnide)
		{
			cerr << programName << ": Unexpected database error: " << pnide << "file url: " << this_file->GetURL() << endl;
			exit(1);
		}
		catch (const std::exception &e)
		{
			cerr << programName << ": Error: " << e.what() << endl;
			exit(1);
		}
		catch (...)
		{
			cerr << programName << ": Unexpected error file url: " << this_file->GetURL() << endl;
			exit(1);
		}
	} // end file iterator

	// if any transforms for any files succeeded, we commit
	if (one_success)
	{
		cout<<"Commiting changes...\n";
		pqxx_interface.Commit();
	}

	return 0;
}

