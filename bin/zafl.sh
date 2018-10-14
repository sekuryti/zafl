#!/bin/bash
#
# Pass-through to underlying zipr toolchain command
#
# @todo: make it more user-friendly and have zafl-specific options
#
usage()
{
	echo
	echo "zafl.sh <input_binary> <output_zafl_binary> [options]"
	echo 
	echo "options:"
	echo "     --ida                      Use IDAPro (default)"
	echo "     --rida                     Do not use IDAPro"
	echo "     --stars                    Use STARS (default)"
	echo "     --no-stars                 Do not use STARS"
	echo "     --graph-optimization       Use basic block graph optimizations (default)"
	echo "     --no-graph-optimization    Do not use basic block graph optimizations"
}

if [ "$1" = "-h" -o "$1" = "--help" ];
then
	usage
	exit 0
fi

if [ "$#" -lt 2 ]; then
	usage
	exit 1
fi

input_binary=$(realpath $1)
output_zafl_binary=$2

shift
shift

#ida_or_rida_opt=" "
ida_or_rida_opt=" -s meds_static=off -s rida=on "
stars_opt=" -o zafl:--stars "
graph_opt=" -o zafl:-g "

other_args=""
# parse args
while [[ $# -gt 0 ]]
do
	key="$1"

	case $key in
		-h|--help)
			usage
			exit 0
			;;
		--ida)
			ida_or_rida_opt=" "
			shift
			;;
		--rida)
			ida_or_rida_opt=" -s meds_static=off -s rida=on "
			shift
			;;
		-s | --stars)
			stars_opt=" -o zafl:--stars "
			shift
			;;
		-S | --no-stars)
			stars_opt=" "
			shift
			;;
		-g | --graph-optimization)
			graph_opt=" -o zafl:-g "
			shift
			;;
		-G | --no-graph-optimization)
			graph_opt=" "
			shift
			;;
		-v | --verbose)
			verbose_opt=" -o zafl:-v "
			shift
			;;
    		*)    # unknown option
			other_args="$other_args $1"         
			shift # past argument
			;;
esac
done

# find main
main_addr=""
tmp_objdump=/tmp/$$.objdump
objdump -d $input_binary > $tmp_objdump
grep "<main>:" $tmp_objdump >/dev/null 2>&1
if [  $? -eq 0 ]; then
	echo Zafl: Detected main program in $input_binary
else
	grep -B1 "libc_start_main@" $tmp_objdump >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		grep -B1 start_main $tmp_objdump | grep rdi | grep rip >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			ep=$(readelf -h $input_binary | grep -i "entry point" | cut -d'x' -f2)
			if [ ! -z $ep ]; then
				echo "Zafl: Main exec is PIE... use entry point address (0x$ep) for fork server"
				options=" $options -o zafl:'-e 0x$ep'"
			else
				echo "Zafl: error finding entry point address"
				exit 1
			fi
		else
			main_addr=$(grep -B1 libc_start_main@plt $tmp_objdump | grep mov | grep rdi | cut -d':' -f2 | cut -d'm' -f2 | cut -d',' -f1 | cut -d'x' -f2)
			if [ "$main_addr" = "" ]; then 
				echo "Zafl: Error inferring main"
				exit 1
			fi

			echo "Zafl: Inferring main to be at: 0x$main_addr"
			options=" $options -o zafl:'-e 0x$main_addr'"
		fi
	else
		echo "Zafl: no main() detected, probably a library ==> no fork server"
	fi
fi
rm $tmp_objdump

echo "Zafl: Transforming input binary $input_binary into $output_zafl_binary"
cmd="$PSZ $input_binary $output_zafl_binary $ida_or_rida_opt -c move_globals=on -c zafl=on -o move_globals:--elftables-only -o zipr:--traceplacement:on $stars_opt $graph_opt $verbose_opt $options $other_args"
echo "Zafl: Issuing command: $cmd"
eval $cmd
if [ $? -eq 0 ]; then
	ldd $output_zafl_binary | grep -e libzafl -e libautozafl >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo
		echo Zafl: success. Output file is: $output_zafl_binary
		echo
	else
		ldd $output_zafl_binary
		echo
		echo Zafl: error: output binary does not show a dependence on the Zafl support library
		exit 1
	fi

	ldd -d $output_zafl_binary | grep symbol | grep 'not defined' >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo Zafl: error: something went wrong in resolving Zafl symnbols
		exit 1
	fi
else
	echo Zafl: error transforming input program
	exit 1
fi

