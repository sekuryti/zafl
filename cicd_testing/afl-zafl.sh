#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl
echo "Core pattern settings"
save_core_pattern=/tmp/$(whoami).core_pattern
cat /proc/sys/kernel/core_pattern > $save_core_pattern

echo "Setup core pattern for afl"
$ZAFL_HOME/zfuzz/util/afl_setup_core_pattern.sh

$ZAFL_HOME/zfuzz/test/strings/test_strings.sh

echo "Restore original core pattern"
cat $save_core_pattern > /proc/sys/kernel/core_pattern

rm $save_core_pattern
