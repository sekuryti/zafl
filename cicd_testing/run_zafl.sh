#/bin/bash

display_license_agreement()
{
	echo
	echo "BY USING THIS DOCKER IMAGE, YOU AGREE TO BE BOUND BY THE"
	echo "CREATIVE COMMONS ATTRIBUTION-NONCOMMERCIAL 4.0 INTERNATIONAL LICENSE"
	echo
}

display_license()
{
	echo
	echo "This docker container is made available to the public by Zephyr Software"
	echo "(contact: jwd@zephyr-software.com) under the Creative Commons Attribution-"
	echo " NonCommercial 4.0 International license (CC BY-NC 4.0)."
	echo
	echo "https://creativecommons.org/licenses/by-nc/4.0/legalcode"
	echo
	echo "Linux, Gcc, and other relevant open source projects are licensed under their"
	echo "own license and are exempt from this license statement."
	echo
}

main()
{
	# Start postgers and give it time to startup
	sudo service postgresql start >/dev/null 2>&1
	
	local retry_count=0 
	while ! pg_isready > /dev/null 2>&1 && (( $retry_count < 60 )) ; do
		retry_count=$(expr retry_count +  1)
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
