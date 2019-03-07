#!/bin/bash

#
# Invoke underlying Zipr toolchain with Zafl step and parameters
#

if [ ! -z $TERM ] && [ "$TERM" != "dumb" ]; then
	RED=`tput setaf 1`
	GREEN=`tput setaf 2`
	YELLOW=`tput setaf 3`
	ORANGE=`tput setaf 214`
	MAGENTA=`tput setaf 5`
	NC=`tput sgr0`
fi

usage()
{
	echo
	echo "zafl.sh <input_binary> <output_zafl_binary> [options]"
	echo 
	echo "options:"
	echo "     -s, --stars                             Use STARS (default)"
	echo "     -S, --no-stars                          Do not use STARS"
	echo "     -g, --graph-optimization                Use control flow graph optimizations"
	echo "     -G, --no-graph-optimization             Do not use control flow graph optimizations (default)"
	echo "     -d, --domgraph-optimization             Use Dominator graph optimizations"
	echo "     -D, --no-domgraph-optimization          Do not use Dominator graph optimizations (default)"
	echo "     -t, --tempdir <dir>                     Specify location of analysis results directory"
	echo "     -e, --entry                             Specify fork server entry point"
	echo "     -E, --exit                              Specify fork server exit point(s)"
	echo "     -u, --untracer                          Specify untracer instrumentation"
	echo "     -c, --enable-breakup-critical-edges     Breakup critical edges"
	echo "     -C, --disable-breakup-critical-edges    Do not breakup critical edges (default)"
	echo "     -f, --fork-server-only                  Fork server only"
	echo "     -m, --enable-fixed-map [<address>]      Use fixed address for tracing map (<address> must be hex and page-aligned, e.g., 0x10000)"
	echo "     -M, --disable-fixed-map                 Disable fixed address tracing map"
	echo "     -i, --enable-floating-instrumentation   Select best instrumentation point within basic block (default)"
	echo "     -I, --disable-floating-instrumentation  Use first instruction for instrumentation in basic blocks"
	echo "     --enable-context-sensitivity <style>    style={callsite,function} only function supported currently (off by default)"
	echo "     -r, --random-seed <value>               Specify random seed"
#	echo "     -l, --enable-locality                   Maintain code locality (best effort) when instrumenting binary"
#	echo "     -L, --disable-locality                  Randomized layout when instrumenting binary"
	echo "     -w, --whitelist <file>                  Specify function whitelist (one function per line)"
	echo "     -b, --blacklist <file>                  Specify function blacklist (one function per line)"
	echo "     --enable-split-compare                  enable laf-intel split compare for integer constants"
	echo "     --disable-split-compare                 disable laf-intel split compare for integer constants"
	echo "     --enable-split-branch                   enable laf-intel split branch for integer constants"
	echo "     --disable-split-branch                  disable laf-intel split branch for integer constants"
	echo "     -v                                      Verbose mode" 
	echo 
}

ida_or_rida_opt=" -c rida "
stars_opt=" -o zax:--stars "
zax_opt=""
other_args=""
#float_opt=""
float_opt=" -o zax:--enable-floating-instrumentation "
context_sensitivity_opt=""
trace_opt=""
zipr_opt=""
random_seed=""
laf_opt=""


me=$(whoami)
tmp_dir=/tmp/${me}/$$
mkdir -p $tmp_dir

# by default, use fixed address for map
trace_map_address="0x10000"
ZAFL_TM_ENV=""

cleanup()
{
	if [ ! -z "$tmp_dir" ]; then
		rm -fr $tmp_dir
	fi
}

log_msg()
{
	echo -e "${GREEN}Zafl: $1 ${NC}"
}

log_warning()
{
	echo -e "${ORANGE}Zafl: $1 ${NC}"
}

log_error()
{
	echo -e "${RED}Zafl: $1 ${NC}"
}

log_error_exit()
{
	log_error "$1"
	cleanup
	exit 1
}

parse_args()
{
	PARAMS=""
	while (( "$#" )); do
		key="$1"

		case $key in
			-h|--help)
				usage
				exit 0
				;;
			--ida)
				ida_or_rida_opt=" -c meds_static=on -s rida=off "
				shift
				;;
			--rida)
				ida_or_rida_opt=" -s meds_static=off -c rida=on "
				shift
				;;
			-s | --stars)
				stars_opt=" -o zax:--stars "
				shift
				;;
			-S | --no-stars)
				stars_opt=" "
				float_opt=" -o zax:--disable-floating-instrumentation "
				shift
				;;
			-g | --graph-optimization)
				zax_opt=" $zax_opt -o zax:-g "
				shift
				;;
			-G | --no-graph-optimization)
				zax_opt=" $zax_opt -o zax:-G "
				shift
				;;
			-d | --domgraph-optimization)
				zax_opt=" $zax_opt -o zax:-d "
				shift
				;;
			-D | --no-domgraph-optimization)
				zax_opt=" $zax_opt -o zax:-D "
				shift
				;;
			-e | --entry)
				shift
				entry_opt=" -o zax:\"-e $1\" "
				shift
				;;
			-E | --exit)
				shift
				exit_opt=" $exit_opt -o zax:\"-E $1\" "
				shift
				;;
			-v | --verbose)
				verbose_opt=" -o zax:-v "
				shift
				;;
			-t | --tempdir)
				shift
				other_args=" --tempdir $1"
				shift
				;;
			-w | --whitelist)
				shift
				zax_opt=" $zax_opt -o zax:\"--whitelist $1 \""
				shift
				;;
			-b | --blacklist)
				shift
				zax_opt=" $zax_opt -o zax:\"--blacklist $1 \""
				shift
				;;
			-u | --untracer)
				zax_opt=" $zax_opt -o zax:--untracer "
				shift
				;;
			-c | --enable-breakup-critical-edges)
				zax_opt=" $zax_opt -o zax:-c "
				shift
				;;
			-C | --disable-breakup-critical-edges)
				zax_opt=" $zax_opt -o zax:-C "
				shift
				;;
			-f | --fork-server-only)
				ZAFL_LIMIT_END=0
				export ZAFL_LIMIT_END
				log_warning "Fork Server Only mode: no block-level instrumentation will be performed"
				shift
				;;
			-m | --enable-fixed-map)
				shift
				case $1 in
					0x*)
						trace_map_address="$1"
						shift
					;;
				esac
				ZAFL_TM_ENV="ZAFL_TRACE_MAP_FIXED_ADDRESS=$trace_map_address"
				;;
			-M | --disable-fixed-map)
				unset ZAFL_TRACE_MAP_FIXED_ADDRESS
				ZAFL_TM_ENV=""
				shift
				;;
			-i | --enable-floating-instrumentation)
				float_opt=" -o zax:--enable-floating-instrumentation "
				shift
				;;
			-I | --disable-floating-instrumentation)
				float_opt=" -o zax:--disable-floating-instrumentation "
				shift
				;;
			--enable-context-sensitivity)
				shift
				case $1 in
					function)
						zax_opt=" -o zax:\"--enable-context-sensitivity function\" "
						shift
					;;
					callsite)
						zax_opt=" -o zax:\"--enable-context-sensitivity callsite\" "
						echo "Error: context sensitivity <callsite> currently unsupported"
						exit 1
					;;
					*)
						echo "Error: must specify function or callsite for context sensitivity"
						exit 1
					;;
				esac
				;;
			-r | --random-seed)
				shift
				random_seed="$1"
				zax_opt=" $zax_opt -o zax:\"--random-seed $random_seed\" "
				zipr_opt=" $zipr_opt --step-option zipr:\"--zipr:seed $random_seed\" "
				shift
				;;
			-l | --enable-locality)
				trace_opt=" --step-option zipr:--traceplacement:on --step-option zipr:true "
				shift
				;;
			-L | --disable-locality)
				trace_opt=""
				shift
				;;
			--enable-split-compare)
				laf_opt=" $laf_opt -o laf:--enable-split-compare "
				shift
				;;
			--disable-split-compare)
				laf_opt=" $laf_opt -o laf:--disable-split-compare "
				shift
				;;
			--enable-split-branch)
				laf_opt=" $laf_opt -o laf:--enable-split-branch "
				shift
				;;
			--disable-split-branch)
				laf_opt=" $laf_opt -o laf:--disable-split-branch "
				shift
				;;
			-*|--*=) # unsupported flags
				echo "Error: Unsupported flag $1" >&2
				exit 1
				;;
    			*) # preserve positional arguments
				PARAMS="$PARAMS $1"
				shift
				;;
		esac
	done

	eval set -- "$PARAMS"
	positional=($PARAMS)
	input_binary=${positional[0]}
	output_zafl_binary=${positional[1]}

	if [ -z $input_binary ]; then
		usage
		log_error_exit "You must specify an input binary to be protected"
	fi

	if [ -z $output_zafl_binary ]; then
		usage
		log_error_exit "You must specify the name of the output binary"
	fi

	input_binary=$(realpath $input_binary)
}

find_main()
{
	main_addr=""
	tmp_objdump=$tmp_dir/tmp.objdump
	tmp_main=$tmp_dir/tmp.main

	objdump -d $input_binary > $tmp_objdump

	grep "<main>:" $tmp_objdump > $tmp_main

	if [  $? -eq 0 ]; then
		main_addr=$(cut -d' ' -f1 $tmp_main)
		log_msg "Detected main at: 0x$main_addr"
		options=" $options -o zax:'-e 0x$main_addr'"
	else
		grep -B1 "libc_start_main@" $tmp_objdump >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			grep -B1 start_main $tmp_objdump | grep rdi | grep rip >/dev/null 2>&1
			if [ $? -eq 0 ]; then
				ep=$(readelf -h $input_binary | grep -i "entry point" | cut -d'x' -f2)
				if [ ! -z $ep ]; then
					log_msg "main exec is PIE... use entry point address (0x$ep) for fork server"
					options=" $options -o zax:'-e 0x$ep'"
				else
					log_error_exit "error finding entry point address"
				fi
			else
				grep "libc_start_main" $tmp_objdump | grep ">:" | grep -e -v "@plt" -v "jmp" >/dev/null 2>&1
				if [ $? -eq 0 ]; then
					log_msg "Detected libc: no main"
					rm $tmp_objdump
					return
				fi

				main_addr=$(grep -B1 libc_start_main@plt $tmp_objdump | grep mov | grep rdi | cut -d':' -f2 | cut -d'm' -f2 | cut -d',' -f1 | cut -d'x' -f2)
				if [ "$main_addr" = "" ]; then 
					log_error_exit "error inferring main"
				fi

				log_msg "inferring main to be at: 0x$main_addr"
				options=" $options -o zax:'-e 0x$main_addr'"
			fi
		else
			log_warning "no main() detected, probably a library ==> no automated insertion of fork server"
		fi
	fi
	rm $tmp_objdump >/dev/null 2>&1
}

verify_zafl_symbols()
{
	# verify library dependency set
	ldd $1 | grep -e libzafl -e libautozafl >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		log_msg "success. Output file is: $1"
	else
		ldd $1
		log_error_exit "output binary does not show a dependence on the Zafl support library"
	fi

	# sanity check symbols in zafl library resolve
	ldd -d $1 | grep symbol | grep 'not defined' >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		log_error_exit "something went wrong in resolving Zafl symbols"
	fi
}

parse_args $*
if [ -z "$entry_opt" ]; then
	find_main
else
	options=" $options $entry_opt "
fi

if [ ! -z "$exit_opt" ]; then
	options=" $options $exit_opt "
fi

#
# Execute Zipr toolchain with Zafl options
#
log_msg "Transforming input binary $input_binary into $output_zafl_binary"

optional_step=""
if [ ! -z "$laf_opt" ];
then
	optional_step=" -c laf=on $laf_opt "
fi

zax_opt=" $zax_opt $float_opt "
cmd="$ZAFL_TM_ENV $PSZ $input_binary $output_zafl_binary $ida_or_rida_opt -c move_globals=on $optional_step -c zax=on -o move_globals:--elftables-only $stars_opt $zax_opt $verbose_opt $options $other_args $trace_opt $zipr_opt"


if [ ! -z "$ZAFL_TM_ENV" ]; then
	log_msg "Trace map will be expected at fixed address"
	if [ -z $ZAFL_TRACE_MAP_FIXED_ADDRESS ]; then
		log_warning "When running afl-fuzz, make sure that the environment variable ZAFL_TRACE_MAP_FIXED_ADDRESS is exported and set properly to (otherwise the instrumented binary will crash):"
		log_warning "export $ZAFL_TM_ENV"
	fi
else
	if [ ! -z $ZAFL_TRACE_MAP_FIXED_ADDRESS ]; then
		log_msg "Trace map will be at fixed address: $ZAFL_TRACE_MAP_FIXED_ADDRESS"
	fi
fi

log_msg "Issuing command: $cmd"
eval $cmd
if [ $? -eq 0 ]; then
	verify_zafl_symbols $output_zafl_binary
else
	log_error_exit "error transforming input program"
fi

