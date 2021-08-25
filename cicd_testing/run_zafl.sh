#/bin/bash

display_license_agreement()
{
	echo
	echo "BSD 3-Clause License"
	echo
	echo "Copyright (c) 2018-2021, Zephyr Software LLC"
	echo "All rights reserved."
	echo
	echo "See the full license here:"
	echo "https://git.zephyr-software.com/opensrc/zafl/-/blob/master/LICENSE"
	echo
}

display_license()
{
	echo
	echo "This docker container is made available to the public by Zephyr Software"
	echo "(contact: jwd@zephyr-software.com) under the BSD #-Clause License"
	echo
	echo "See the full license here:"
	echo "https://git.zephyr-software.com/opensrc/zafl/-/blob/master/LICENSE"
	echo
	echo "Linux, Gcc, and other relevant open source projects are licensed under their"
	echo "own license and are exempt from this license statement."
	echo
}

main()
{
	# Start postgers and give it time to startup
	sudo service postgresql start >/dev/null 2>&1
	
	echo "Waiting for postgres to be ready"
	local retry_count=0 
	while ! pg_isready && [[ $retry_count -lt 600 ]] ; do
		retry_count=$(expr $retry_count +  1)
		sleep 1
	done	

	export ZAFL_INSTALL=/home/zuser/zafl
	cd $ZAFL_INSTALL
	source ./set_env_vars 

	cd /tmp 

	display_license

	echo

	$ZAFL_HOME/bin/zafl.sh "$@" 

	res=$?
	if [[ $res  != 0 ]]; then
		echo Failed to Zafl. Printing logs
		cat  peasoup*/logs/*
		echo Failed to Zafl.  Logs were printed.
		exit 1
	fi

	display_license_agreement
	exit 0
}

main "$@" 
