#!/bin/bash

cd $AFL_TRANSFORMS
scons -c || exit

cd $ZFUZZ_HOME/libzafl/src
scons -c || exit
scons autozafl=1 -c || exit

cd $ZFUZZ_HOME
rm -fr afl
