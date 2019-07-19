#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl

echo "Setup afl - ZAFL_HOME=$ZAFL_HOME "
$ZAFL_HOME/util/setup-afl.sh
sudo $ZAFL_HOME/util/afl_setup_core_pattern.sh

echo "Test various zafl configurations"
$ZAFL_HOME/test/strings/test_strings.sh
$ZAFL_HOME//test/bc/test_bc.sh
$ZAFL_HOME//test/od/test_od.sh

echo "Test graph optimizations"
$ZAFL_HOME/tools/zax/test/test_graph.sh
$ZAFL_HOME/tools/zax/test/test_context.sh
$ZAFL_HOME/tools/zax/test/test_context_recursion.sh

echo "Test laf"
$ZAFL_HOME/tools/laf/test/run_tests.sh

$ZAFL_HOME/test/bc.fixed_map/test_bc.sh
$ZAFL_HOME/test/sha256sum/test_sha256sum.sh

echo "Test zuntracer configurations"
$ZAFL_HOME/test/ls.zuntracer/test_ls.sh

