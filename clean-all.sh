#!/bin/bash

cd $AFL_TRANSFORMS
scons -c || exit

cd $ZEPHYR_FUZZING_HOME/libzafl/src
scons -c || exit
