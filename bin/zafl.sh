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
	echo "     --ida         Use IDAPro"
	echo "     --rida        (default) Do not use IDAPro"
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

input_binary=$1
output_zafl_binary=$2

shift
shift

# default is rida
#ida_or_rida=" -s meds_static=off -s rida=on "

# default is ida
ida_or_rida=" "

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
			ida_or_rida=" "
			shift
			;;
		--rida)
			ida_or_rida=" -s meds_static=off -s rida=on "
			shift
			;;
    		*)    # unknown option
			other_args="$other_args $1"         
			shift # past argument
			;;
esac
done

# find main
tmp_objdump=/tmp/$$.objdump
objdump -d $input_binary > $tmp_objdump
grep "<main>:" $tmp_objdump >/dev/null 2>&1
if [ ! $? -eq 0 ]; then
	grep -B1 libc_start_main@plt $tmp_objdump >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		grep -B1 start_main $tmp_objdump | grep rdi | grep rip
		if [ $? -eq 0 ]; then
			echo "Zafl: Main exec is PIE... unable to infer address of main. Automatically insert fork server (not as efficient as inferring main though)"
			options=" $options -o zafl:--autozafl "
		else
			main_addr=$(grep -B1 libc_start_main@plt $tmp_objdump | grep mov | grep rdi | cut -d':' -f2 | cut -d'm' -f2 | cut -d',' -f1 | cut -d'x' -f2)
			if [ "$main_addr" = "" ]; then 
				echo "Zafl: Error inferring main"
				exit 1
			fi

			echo "Zafl: Inferring main to be at: 0x$main_addr"
			options=" $options -o zafl:'-e 0x$main_addr'"
		fi
	fi
fi
rm $tmp_objdump

echo "Zafl: Transforming input binary $input_binary into $output_zafl_binary"
#cmd="$PSZ $input_binary $output_zafl_binary -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zafl:--stars $*"
cmd="$PSZ $input_binary $output_zafl_binary $ida_or_rida -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zafl:--stars $options $*"
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
else
	echo Zafl: error transforming input program
	exit 1
fi

