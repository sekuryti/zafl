#!/bin/bash

#
# Invoke underlying Zipr toolchain with Zafl step and parameters
#

RED=`tput setaf 1`
GREEN=`tput setaf 2`
YELLOW=`tput setaf 3`
ORANGE=`tput setaf 214`
MAGENTA=`tput setaf 5`
NC=`tput sgr0`

usage()
{
	echo
	echo "zafl.sh <input_binary> <output_zafl_binary> [options]"
	echo 
	echo "options:"
	echo "     -s, --stars                    Use STARS (default)"
	echo "     -S, --no-stars                 Do not use STARS"
	echo "     -g, --graph-optimization       Use basic block graph optimizations"
	echo "     -G, --no-graph-optimization    Do not use basic block graph optimizations (default)"
	echo "     -t, --tempdir                  Specify location of analysis directory"
	echo "     -e, --entry                    Specify fork server entry point"
	echo "     -E, --exit                     Specify fork server exit point(s)"
}

ida_or_rida_opt=" -s meds_static=off -s rida=on "
stars_opt=" -o zax:--stars "
graph_opt=" "
other_args=""

me=$(whoami)
tmp_dir=/tmp/${me}/$$
mkdir -p $tmp_dir

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
				shift
				;;
			-g | --graph-optimization)
				graph_opt=" -o zax:-g "
				shift
				;;
			-G | --no-graph-optimization)
				graph_opt=" "
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
	input_binary=$(realpath ${positional[0]})
	output_zafl_binary=${positional[1]}

	if [ -z $input_binary ]; then
		usage
		log_error_exit "You must specify an input binary to be protected"
	fi

	if [ -z $output_zafl_binary ]; then
		usage
		log_error_exit "You must specify the name of the output binary"
	fi
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
		log_msg "detected main at: 0x$main_addr"
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

#cmd="$PSZ $input_binary $output_zafl_binary $ida_or_rida_opt -c move_globals=on -c zax=on -o move_globals:--elftables-only -o zipr:--traceplacement:on $stars_opt $graph_opt $verbose_opt $options $other_args"
cmd="$PSZ $input_binary $output_zafl_binary $ida_or_rida_opt -c move_globals=on -c zax=on -o move_globals:--elftables-only $stars_opt $graph_opt $verbose_opt $options $other_args"

log_msg "Issuing command: $cmd"
eval $cmd
if [ $? -eq 0 ]; then
	verify_zafl_symbols $output_zafl_binary
else
	log_error_exit "error transforming input program"
fi

