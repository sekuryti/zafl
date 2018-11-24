#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl
echo "Core pattern settings"
cat /proc/sys/kernel/core_pattern

$ZAFL_HOME/zfuzz/test/strings/test_strings.sh


