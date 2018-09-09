#!/bin/bash
#
# Pass-through to underlying zipr toolchain command
#
# @todo: make it more user-friendly and have zafl-specific options
#
input_binary=$1
output_zafl_binary=$2

shift
shift

# find main
tmp_objdump=/tmp/$$.objdump
objdump -d $input_binary > $tmp_objdump
grep "<main>:" $tmp_objdump >/dev/null 2>&1
if [ ! $? -eq 0 ]; then
	grep -B1 libc_start_main@plt $tmp_objdump >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		main_addr=$(grep -B1 libc_start_main@plt $tmp_objdump | grep mov | grep rdi | cut -d':' -f2 | cut -d'm' -f2 | cut -d',' -f1 | cut -d'x' -f2)
		if [ "$main_addr" = "" ]; then 
			echo "Zafl: Error inferring main"
			exit 1
		fi

		echo "Zafl: Inferring main to be at: 0x$main_addr"
		options=" -o zafl:'-e 0x$main_addr'"
	fi
fi

echo "Zafl: Transforming input binary $input_binary into $output_zafl_binary"
#cmd="$PSZ $input_binary $output_zafl_binary -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zafl:--stars $*"
cmd="$PSZ $input_binary $output_zafl_binary -s meds_static=off -s rida=on -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zafl:--stars $options $*"
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
fi
