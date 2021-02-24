#!/bin/bash

set -e
set -x

main()
{
        local orig_dir=$(pwd)

        # gather info for debugging later, probably not necessary
        pwd
        hostname
        whoami
        env|grep "^CICD"

	# sync submodules 
        git submodule sync
        git submodule update --init --recursive

	# rsync with /tmp for caching.  this is a very fast copy operation.
	time rsync -a --exclude='.git'  $CICD_TO_TEST_DIR/ /tmp/zafl_tmp

        # puts ps_zipr (and all submodules) in CICD_MODULE_WORK_DIR
        cicd_setup_module_dependency opensrc/zipr.git zafl_test

        # Build/run $PSZ, test result
        cd $CICD_MODULE_WORK_DIR/zafl_test
        source set_env_vars
        sudo ./get-peasoup-packages.sh all

        # remove pedi files so that rebuilding includes re-doing pedi setup.
	# this is quick enough that we can do it on every test.
        $PEDI_HOME/pedi -c -m manifest.txt || true # ignore errors in cleanup
        ./build-all.sh
        dropdb $PGDATABASE 2>/dev/null || true  # ignore errors in DB cleanup

	# make sure postgres is clean/setup
       	./postgres_setup.sh

	# build zafl
        cd /tmp/zafl_tmp
        source set_env_vars
	scons -j 3

}

main "$@"

