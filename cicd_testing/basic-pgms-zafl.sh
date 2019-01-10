#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $PEASOUP_HOME/tests; make clean; 

# test kill_deads as zafl relies on it
./test_cmds.sh -c "kill_deads" -a "bzip2 tar tcpdump" -l

if [[ $CICD_NIGHTLY == 1 ]] ; then

	# test zafl config across all default apps
	./test_cmds.sh -c "zafl" -l

	# test other zafl configs on various apps
	./test_cmds.sh -c "zafl_ida zafl_nostars zafl_opt_graph zafl_untracer" -a "bzip2 tar tcpdump" -l

else
	./test_cmds.sh -c "zafl" -a "bzip2 tar" -l
fi
