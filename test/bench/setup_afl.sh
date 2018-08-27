#!/bin/bash

SCRIPT=$(readlink -f $0)
MYDIR=$(dirname $SCRIPT)

cd $MYDIR

source binutils.spec

for b in $binutils_binaries
do
	for t in $tools
	do
		pushd ${b}_${t}
		if [ ! -d in ]; then
			mkdir in
		fi
		echo "1" > in/1

		if [ -d out ]; then
			rm -fr out
		fi
		popd
	done
done
