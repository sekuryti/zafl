#!/bin/bash
input_binary=$1
output_zafl_binary=$2

shift
shift

echo "Zafl: Transforming input binary $input_binary into $output_zafl_binary"
$PSZ $input_binary $output_zafl_binary -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zafl:--stars $*
