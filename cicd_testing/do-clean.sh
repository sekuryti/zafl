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

		rm -rf /tmp/zafl_test
	fi
}

main "$@"

