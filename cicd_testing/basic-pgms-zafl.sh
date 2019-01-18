#!/bin/bash
set -e
set -x


main()
{

	cd /tmp/zafl_test
	source set_env_vars
	cd $PEASOUP_HOME/tests; make clean; 

	local benchmarks=""

	if [[ $CICD_NIGHTLY == 1 ]] ; then
		benchmarks="tcpdump ncal bzip2 tar"
	else
		benchmarks="bzip2 tar"
	fi

	# test other zafl configs on various apps
	./test_cmds.sh -c " zafl kill_deads.rida zafl_nostars zafl_opt_graph zafl_untracer zafl_untracer_critical_edges " -a "$benchmarks" -l 

}

main "$@"
