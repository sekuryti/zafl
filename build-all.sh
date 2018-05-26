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
