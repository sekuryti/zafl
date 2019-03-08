#!/bin/bash

echo
echo "Building Fuzzing Support ($ZFUZZ_HOME)"
echo

if [ -z "$ZFUZZ_HOME" ]; then
	echo "error: environment var $ZFUZZ_HOME is undefined"
	exit 1
fi

cd $ZFUZZ_HOME

afl_loc=$(which afl-fuzz)
if [ -z "$afl_loc" ]; then
	echo
	echo Setup AFL
	echo 
	wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
	tar -xzvf afl-latest.tgz && rm afl-latest.tgz
	if [ -d afl ]; then
		rm -fr afl
	fi
	mv afl-* afl
	cd afl
	make
	sudo make install
#	cd qemu_mode && ./build_qemu_support.sh

	# afl wants this
	sudo $ZFUZZ_HOME/util/afl_setup_core_pattern.sh
fi
