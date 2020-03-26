#!/bin/bash
set -e
set -x

main()
{
	env
	pwd
	ls -lt /tmp

	cd /tmp/zafl_test
	source set_env_vars

	exit 0

	cd $PEASOUP_HOME/tests; make clean; 

	local benchmarks=""
	local configs=""

	if [[ $CICD_NIGHTLY == 1 ]] ; then
		benchmarks="tcpdump ncal bzip2 tar"
		configs="zafl_nostars zafl_untracer_critical_edges zafl_context_sensitive_laf_domgraph_optgraph"
	else
		benchmarks="readelf touch"
		configs="zafl_untracer_critical_edges zafl_context_sensitive_laf_domgraph_optgraph"
	fi

	# test other zafl configs on various apps
	./test_cmds.sh -c "$configs" -a "$benchmarks" -l 

	# zafl-specific test stressing compiler options
	cd $ZAFL_HOME/test/eightqueens
	./test_8q.sh
}

main "$@"
