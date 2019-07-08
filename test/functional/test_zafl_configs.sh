echo "Test various zafl configs"
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1

zafl_configs="zafl zafl_context_sensitive_laf_domgraph_optgraph zafl_nostars"

pushd $PEASOUP_HOME/tests

make clean
./test_cmds.sh -c "$zafl_configs"

popd



