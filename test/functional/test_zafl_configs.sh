echo "Test various zafl configs"

zafl_configs="zafl zafl_context_sensitive_laf_domgraph_optgraph zafl_nostars"

pushd $PEASOUP_HOME/tests

make clean
./test_cmds.sh -c "$zafl_configs"

popd



