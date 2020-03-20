#!/bin/bash

set -e
set -x

main()
{

        if [[ $CICD_NIGHTLY == 1 ]] ; then
		# gather info for debugging later, probably not necessary 
		pwd
		hostname
		whoami
		env|grep CICD

		# remove the cache
		rm -rf $CICD_MODULE_WORK_DIR/zafl_test /tmp/zafl_tmp
	fi
}

main "$@"

