#!/bin/bash

echo
echo "Building Fuzzing Support"
echo

SCONSDEBUG=""
if [[ "$*" =~ "--debug" ]]; then
	SCONSDEBUG=" debug=1 "
fi

cd $ZFUZZ_HOME
if [ ! -e afl ]; then
	echo
	echo Setup AFL
	echo 
	wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
	tar -xzvf afl-latest.tgz && rm afl-latest.tgz
	mv afl-* afl
	cd afl && make
	cd qemu_mode && ./build_qemu_support.sh
fi

cd $AFL_TRANSFORMS
scons $SCONSDEBUG -j 3 || exit

cd $ZFUZZ_HOME/libzafl
scons
scons autozafl=1

