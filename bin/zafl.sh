#!/bin/bash
input_binary=$1
output_zafl_binary=$2

shift
shift

echo "Zafl: Transforming input binary $input_binary into $output_zafl_binary"
cmd="$PSZ $input_binary $output_zafl_binary -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zafl:--stars $*"
echo "Zafl: Issuing command: $cmd"
eval $cmd
if [ $? -eq 0 ]; then
	ldd $output_zafl_binary | grep libzafl >/dev/null 2>&1
	if [ ! $? -eq 0 ]; then
		echo "Zafl: error: output binary does not show a dependence on the Zafl support library"
	fi
fi
