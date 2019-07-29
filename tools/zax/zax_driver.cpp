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

#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "zax.hpp"
#include "zuntracer.hpp"

using namespace std;
using namespace IRDB_SDK;
using namespace Zafl;

static void usage(char* name)
{
	cerr<<"Usage: "<<name<<" <variant_id>\n";
	cerr<<"\t[--verbose | -v]                               Verbose mode                  "<<endl;
	cerr<<"\t[--help,--usage,-?,-h]                         Display this message          "<<endl;
	cerr<<"\t[--entrypoint|-e {<funcName>|<hex_address>}]   Specify where to insert fork server (defaults to main if found)"<<endl;
	cerr<<"\t[--exitpoint|-E {<funcName>|<hex_address>}]    Specify where to insert exit"<<endl;
	cerr<<"\t[--whitelist|-w whitelistFile]                 Specify list of functions/addresses to instrument"<<endl;
	cerr<<"\t[--blacklist|-b blacklistFile]                 Specify list of functions/addresses to omit"<<endl;
	cerr<<"\t[--fixed-map-address <addr>]                   Enable fixed-mapping addressing and Specify the address to use for the map [Default 0x10000]"<<endl;
	cerr<<"\t[--no-fixed-map]                               Disablw fixed-mapping addressing"<<endl;
	cerr<<"\t[--stars|-s]                                   Enable STARS optimizations    "<<endl;
	cerr<<"\t[--enable-bb-graph-optimization|-g]            Elide instrumentation if basic block has 1 successor"<<endl;
	cerr<<"\t[--disable-bb-graph-optimization|-G]           Elide instrumentation if basic block has 1 successor"<<endl;
	cerr<<"\t[--autozafl|-a]                                Auto-initialize fork server (incompatible with --entrypoint)"<<endl;
	cerr<<"\t[--untracer|-u]                                Untracer-style block-coverage instrumentation"<<endl;
	cerr<<"\t[--enable-critical-edge-breakup|-c]            Breakup critical edges"<<endl;
	cerr<<"\t[--disable-critical-edge-breakup|-C]           Do not breakup critical edges (default)"<<endl;
	cerr<<"\t[--enable-loop-count-instr|-j]                 Insert instrumentation to do afl-style loop counting for zuntracer."<<endl;
	cerr<<"\t[--disable-loop-count-instr|-J]                Do not do -j (default)"<<endl;
	cerr<<"\t[--enable-floating-instrumentation|-i]         Select best instrumentation within basic block"<<endl;
	cerr<<"\t[--disable-floating-instrumentation|-I]        Instrument first instruction in basic blocks"<<endl;
	cerr<<"\t[--enable-context-sensitivity <style>]         Use calling context sensitivity, style={callsite,function}"<<endl;
	cerr<<"\t[--disable-context-sensitivity]                Disable calling context sensitivity"<<endl;
	cerr<<"\t[--random-seed|r <value>]                      Specify random seed"<<endl;
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
	auto domgraph_optimize=false;
	auto forkserver_enabled=true;
	auto untracer_mode=false;
	auto breakup_critical_edges=false;
	auto do_loop_count_instr=false;
	auto floating_instrumentation=false;
	auto context_sensitivity=ContextSensitivity_None;
	auto random_seed = 0U;
	auto fixed_map_address=VirtualOffset_t(0x10000);
	set<string> exitpoints;

	srand(getpid()+time(NULL));

	// Parse some options for the transform
	static struct option long_options[] = {
		{"verbose",                          no_argument,       0, 'v'},
		{"help",                             no_argument,       0, 'h'},
		{"usage",                            no_argument,       0, '?'},
		{"stars",                            no_argument,       0, 's'},
		{"entrypoint",                       required_argument, 0, 'e'},
		{"exitpoint",                        required_argument, 0, 'E'},
		{"whitelist",                        required_argument, 0, 'w'},
		{"blacklist",                        required_argument, 0, 'b'},
		{"fixed-map-address",                required_argument, 0, 'm'},
		{"no-fixed-map",                     no_argument,       0, 'M'},
		{"autozafl",                         no_argument,       0, 'a'},
		{"enable-bb-graph-optimization",     no_argument,       0, 'g'},
		{"disable-bb-graph-optimization",    no_argument,       0, 'G'},
		{"enable-domgraph-optimization",     no_argument,       0, 'd'},
		{"disable-domgraph-optimization",    no_argument,       0, 'D'},
		{"enable-forkserver",                no_argument,       0, 'f'},
		{"disable-forkserver",               no_argument,       0, 'F'},
		{"untracer",                         no_argument,       0, 'u'},
		{"enable-critical-edge-breakup",     no_argument,       0, 'c'},
		{"disable-critical-edge-breakup",    no_argument,       0, 'C'},
		{"enable-loop-count-instr",          no_argument,       0, 'j'},
		{"disable-loop-count-instr",         no_argument,       0, 'J'},
		{"enable-floating-instrumentation",  no_argument,       0, 'i'},
		{"disable-floating-instrumentation", no_argument,       0, 'I'},
		{"enable-context-sensitivity",       required_argument, 0, 'z'},
		{"random-seed",                      required_argument, 0, 'r'},
		{0,0,0,0}
	};
	const char* short_opts="r:z:e:E:w:sv?hagGdDfFucCjJiIm:M";

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
			case 'm':
				fixed_map_address = strtoul(optarg, nullptr, 0);
				break;
			case 'M':
				fixed_map_address = 0;
				break;
			case 'a':
				autozafl=true;
				break;
			case 'g':
				bb_graph_optimize=true;
				break;
			case 'G':
				bb_graph_optimize=false;
				break;
			case 'd':
				domgraph_optimize=true;
				break;
			case 'D':
				domgraph_optimize=false;
				break;
			case 'f':
				forkserver_enabled=true;
				break;
			case 'F':
				forkserver_enabled=false;
				break;
			case 'u':
				untracer_mode=true;
				break;
			case 'c':
				breakup_critical_edges=true;
				break;
			case 'C':
				breakup_critical_edges=false;
				break;
			case 'j':
				do_loop_count_instr=true;
				break;
			case 'J':
				do_loop_count_instr=false;
				break;
			case 'i':
				floating_instrumentation=true;
				break;
			case 'I':
				floating_instrumentation=false;
				break;
			case 'z':
				if (optarg == string("callsite"))
					context_sensitivity=ContextSensitivity_Callsite; // Angora fuzzer style
				else if (optarg == string("function"))
					context_sensitivity=ContextSensitivity_Function;
				else
					context_sensitivity=ContextSensitivity_None;
				break;
			case 'r':
				random_seed = strtoul(optarg, NULL, 0);
				srand(random_seed);
				cout << "Setting random seed to: " << random_seed << endl;
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

	if (floating_instrumentation && !use_stars)
	{
		cerr << "STARS must be turned on when using floating instrumentation" << endl;
		usage(argv[0]);
		exit(1);
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
			const auto zax = (untracer_mode) ?  
				unique_ptr<ZaxBase_t>(new ZUntracer_t(*pqxx_interface, firp.get(), entry_fork_server, exitpoints, use_stars, autozafl)) : 
				unique_ptr<ZaxBase_t>(new Zax_t      (*pqxx_interface, firp.get(), entry_fork_server, exitpoints, use_stars, autozafl)); 

			if (whitelistFile.size()>0)
				zax->setWhitelist(whitelistFile);
			if (blacklistFile.size()>0)
				zax->setBlacklist(blacklistFile);

			zax->setVerbose(verbose);
			zax->setBasicBlockOptimization(bb_graph_optimize);
			zax->setDomgraphOptimization(domgraph_optimize);
			zax->setBasicBlockFloatingInstrumentation(floating_instrumentation);
			zax->setEnableForkServer(forkserver_enabled);
			zax->setBreakupCriticalEdges(breakup_critical_edges);
			zax->setDoLoopCountInstrumentation(do_loop_count_instr);
			zax->setContextSensitivity(context_sensitivity); 
			zax->setFixedMapAddress(fixed_map_address);

			int success=zax->execute();

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
		catch (const DatabaseError_t pnide)
		{
			cerr << programName << ": Unexpected database error: " << pnide << "file url: " << this_file->getURL() << endl;
			exit(1);
		}
		catch (const std::exception &e)
		{
			cerr << programName << ": Error: " << e.what() << endl;
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

