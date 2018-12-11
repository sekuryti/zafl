#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $PEASOUP_HOME/tests; make clean; 

# test kill_deads as zafl relies on it
./test_cmds.sh -c "kill_deads" -a "bzip2 tar tcpdump" -l

# test zafl config across all default apps
./test_cmds.sh -c "zafl" -l

# test other zafl configs on various apps
./test_cmds.sh -c "zafl_ida zafl_nostars zafl_opt_graph" -a "bzip2 tar tcpdump" -l

