#!/bin/bash

echo
echo "Building Fuzzing Plugins"
echo


SCONSDEBUG=""
if [[ "$*" =~ "--debug" ]]; then
	SCONSDEBUG=" debug=1 "
fi

cd $AFL_TRANSFORMS
scons $SCONSDEBUG -j 3 || exit

cd $ZFUZZ_HOME/libzafl/src
scons

cd $ZFUZZ_HOME
if [ ! -e afl ]; then
	echo
	echo Setup AFL
	echo 
	wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
	tar -xzvf afl-latest.tgz
	rm afl-latest.tgz
	mv afl-* afl
	cd afl
	make
fi
