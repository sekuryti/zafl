#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $PEASOUP_HOME/tests; make clean; 

# test zafl config across all default apps
./test_cmds.sh -c "zafl" -l

# test other zafl configs on various apps
./test_cmds.sh -c "zafl_ida zafl_nostars zafl_opt_graph" -a "tar tcpdump bzip2" -l

