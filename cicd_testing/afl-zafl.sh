#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl

echo "Setup afl - ZAFL_HOME=$ZAFL_HOME ZFUZZ_HOME=$ZFULL_HOME"
$ZAFL_HOME/zfuzz/util/setup-afl.sh
sudo $ZAFL_HOME/zfuzz/util/afl_setup_core_pattern.sh

echo "Test various zafl configurations"
$ZAFL_HOME/zfuzz/test/strings/test_strings.sh
$ZAFL_HOME/zfuzz/test/bc/test_bc.sh
$ZAFL_HOME/zfuzz/test/sha256sum/test_sha256sum.sh
$ZAFL_HOME/zfuzz/test/od/test_od.sh

echo "Testing zafl with ZAFL_TRACE_MAP_FIXED_ADDRESS=0x10000"
export ZAFL_TRACE_MAP_FIXED_ADDRESS=0x10000
$ZAFL_HOME/zfuzz/test/strings/test_strings.sh
$ZAFL_HOME/zfuzz/test/bc/test_bc.sh
$ZAFL_HOME/zfuzz/test/sha256sum/test_sha256sum.sh
$ZAFL_HOME/zfuzz/test/od/test_od.sh

echo "Test zuntracer configurations"
$ZAFL_HOME/zfuzz/test/ls.zuntracer/test_ls.sh

