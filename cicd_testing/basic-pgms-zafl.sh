#!/bin/bash
set -e
set -x

main()
{
	# for debugging
	env
	pwd
	ls -lt /tmp
		
	# source PEASOUP env vars
	cd $CICD_MODULE_WORK_DIR/zafl_test
	source set_env_vars

	# source ZAFL env vars
	cd /tmp/zafl_tmp
	source set_env_vars

	cd $PEASOUP_HOME/tests; make clean; 

	local benchmarks=""
	local configs=""

	if [[ $CICD_NIGHTLY == 1 ]] ; then
		# zafl-specific test stressing compiler options
		cd $ZAFL_HOME/test/eightqueens
		./test_8q.sh

		benchmarks="tcpdump ncal bzip2 tar readelf"
		configs="zafl_nostars zafl_untracer_critical_edges zafl_context_sensitive_laf_domgraph_optgraph"
	else
		benchmarks="touch"
		configs="zafl_untracer_critical_edges zafl_context_sensitive_laf_domgraph_optgraph"
	fi

	# test other zafl configs on various apps
	./test_cmds.sh -c "$configs" -a "$benchmarks" -l 
}

main "$@"
