#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl
echo "Core pattern settings"
save_core_pattern=/tmp/$(whoami).core_pattern
sudo cat /proc/sys/kernel/core_pattern > $save_core_pattern

echo "Setup afl - ZAFL_HOME=$ZAFL_HOME ZFUZZ_HOME=$ZFULL_HOME"
sudo -E $ZAFL_HOME/zfuzz/util/setup-afl.sh
sudo -E $ZAFL_HOME/zfuzz/util/afl_setup_core_pattern.sh

echo "Test various zafl configurations"
$ZAFL_HOME/zfuzz/test/strings/test_strings.sh
$ZAFL_HOME/zfuzz/test/bc/test_bc.sh
$ZAFL_HOME/zfuzz/test/sha256sum/test_sha256sum.sh
$ZAFL_HOME/zfuzz/test/od/test_od.sh

echo "Test zuntracer configurations"
$ZAFL_HOME/zfuzz/test/ls.zuntracer/test_ls.sh

