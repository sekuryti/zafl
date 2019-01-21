#!/bin/bash
set -e
set -x


main()
{

	cd /tmp/zafl_test
	source set_env_vars
	cd $PEASOUP_HOME/tests; make clean; 

	local benchmarks=""
	local configs=""

	if [[ $CICD_NIGHTLY == 1 ]] ; then
		benchmarks="tcpdump ncal bzip2 tar"
		configs="kill_deads.rida zafl zafl_nostars zafl_opt_graph zafl_untracer zafl_untracer_critical_edges "
	else
		benchmarks="readelf touch"
		configs="kill_deads.rida zafl zafl_opt_graph zafl_untracer_critical_edges"
	fi

	# test other zafl configs on various apps
	./test_cmds.sh -c "$configs" -a "$benchmarks" -l 

}

main "$@"
