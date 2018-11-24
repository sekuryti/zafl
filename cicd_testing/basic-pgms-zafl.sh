#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $PEASOUP_HOME/tests; make clean; ./test_cmds.sh -c "zafl zafl_ida zafl_nostars zafl_opt_graph"

