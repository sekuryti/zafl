#!/bin/bash

AFL_TIMEOUT=3600
EPOCH_TIMEOUT=3800

SCRIPT=$(readlink -f $0)
MYDIR=$(dirname $SCRIPT)

cd $MYDIR

source binutils.spec

echo
echo "Build and compare different Zafl configurations"
echo

binaries=$binutils_binaries
trials="1 2 3"

# build all the zafl configurations we care about
for b in $binaries
do
	cd $MYDIR/${b}_zafl
	echo building in $PWD
	if [ ! -e $b.zafl ]; then
		$PSZ $b $b.zafl -c move_globals=on -c zafl=on -o move_globals:--elftables 
	fi
	if [ ! -e $b.zafl.stars ]; then
		$PSZ $b $b.zafl.stars -c move_globals=on -c zafl=on -o move_globals:--elftables -o zafl:--stars 
	fi
	if [ ! -e $b.zafl.trace ]; then
		$PSZ $b $b.zafl.trace -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on 
	fi
	if [ ! -e $b.zafl.stars.trace ]; then
		$PSZ $b $b.zafl.stars.trace -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zafl:--stars 
	fi
#	if [ ! -e $b.zafl.stars.relax ]; then
#		$PSZ $b $b.zafl.stars.relax -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--relax:on -o zafl:--stars
#	fi
done

exit 0

for t in $trials
do
	for b in $binaries
	do
		cd $MYDIR/${b}_zafl

		echo "fuzz_map for $b: ${fuzz_map[$b]}"

		# create input seed directory
		mkdir in
		echo "1" > in/1

		rm -fr out.$t.zafl
		nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out.$t.zafl -- ./$b.zafl ${fuzz_map[$b]} &
		nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out.$t.zafl.stars -- ./$b.zafl.stars ${fuzz_map[$b]} &
		nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out.$t.zafl.trace -- ./$b.zafl.trace ${fuzz_map[$b]} &
		nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out.$t.zafl.stars.trace -- ./$b.zafl.stars.trace ${fuzz_map[$b]} &
#		nohup timeout $AFL_TIMEOUT afl-fuzz -i in -o out.$t.zafl.stars.relax -- ./$b.zafl.stars.relax ${fuzz_map[$b]} &

		sleep $EPOCH_TIMEOUT

		pkill afl-fuzz >/dev/null 2>&1
	done
done
