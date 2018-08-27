#!/bin/bash

SCRIPT=$(readlink -f $0)
MYDIR=$(dirname $SCRIPT)

cd $MYDIR

source binutils.spec

echo "Evaluate $binutils_binaries"
AFL_TIMEOUT=3600
EPOCH_TIMEOUT=3800

./setup_afl.sh

for b in $binutils_binaries
do
	echo "fuzz_map for $b: ${fuzz_map[$b]}"

	cd $MYDIR/${b}_aflgcc
	nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out -- ./${b}.aflgcc ${fuzz_map[$b]} &

	cd $MYDIR/${b}_zafl
	nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out -- ./${b}.zafl ${fuzz_map[$b]} &

	cd $MYDIR/${b}_dyninst
	nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out -- ./${b}.dyninst ${fuzz_map[$b]} &

	cd $MYDIR/${b}_qemu
	nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out -Q -- ./${b}.qemu ${fuzz_map[$b]} &

	sleep $EPOCH_TIMEOUT

	grep execs ${b}_*/out/fuzzer_stats
done
