echo "Test various zafl configs"

zafl_configs="zafl zafl_domgraph zafl_domgraph_opt zafl_domgraph_opt_context_sensitive zafl_nostars zafl_laf_domgraph zafl_context_sensitive_laf zafl_context_sensitive_laf_domgraph zafl_context_sensitive_laf_domgraph_optgraph"

$PEASOUP_HOME/tests/test_cmds.sh -c "$zafl_configs" 



