#/bin/bash

main()
{
	sudo service postgresql start >/dev/null 2>&1

	export ZAFL_INSTALL=/home/zuser/zafl
	cd $ZAFL_INSTALL
	source ./set_env_vars 

	cd /tmp 
	$ZAFL_HOME/bin/zafl.sh "$@" 

	res=$?
	if [[ $res  != 0 ]]; then
		echo Failed to Zafl. Printing logs
		cat  peasoup*/logs/*
		echo Failed to Zafl.  Logs were printed.
		exit 1
	fi

	exit 0
}

main "$@" 
