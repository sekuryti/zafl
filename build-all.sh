#!/bin/bash

echo
echo "Building Fuzzing Support"
echo

SCONSDEBUG=""
if [[ "$*" =~ "--debug" ]]; then
	SCONSDEBUG=" debug=1 "
fi

cd $AFL_TRANSFORMS
scons $SCONSDEBUG -j 3 || exit

cd $ZFUZZ_HOME/libzafl
scons
scons autozafl=1

