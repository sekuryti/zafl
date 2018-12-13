#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $ZAFL_HOME/release

echo "Generating releasing"
./generate_release.sh

