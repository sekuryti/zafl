#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars

# Test with afl
$ZAFL_HOME/zfuzz/test/strings/test_strings.sh


