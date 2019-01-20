#!/bin/bash

echo
echo "Building Fuzzing Support"
echo

cd $ZFUZZ_HOME
if [ ! -e afl ]; then
	echo
	echo Setup AFL
	echo 
	wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
	tar -xzvf afl-latest.tgz && rm afl-latest.tgz
	mv afl-* afl
	cd afl
	make
#	cd qemu_mode && ./build_qemu_support.sh

	AFL_PATH=/home/tester/zafl_umbrella/zfuzz/afl
	export AFL_PATH

	# afl wants this
	sudo $ZFUZZ_HOME/util/afl_setup_core_pattern.sh
fi
