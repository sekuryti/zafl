#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd $CICD_TO_TEST_DIR/peasoup_examples/tests
cd $PEASOUP_HOME/tests; make clean; ./test_cmds.sh zafl

