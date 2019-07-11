#/bin/bash

main()
{
	service postgresql start
	export USER=root;
	export ZAFL_INSTALL=/zafl
	cd $ZAFL_INSTALL
	source ./set_env_vars 

	cd /tmp 
	/zafl/bin/zafl.sh "$@" 

	res=$?
	if [[ $res  != 0 ]]; then
		echo
		echo
		echo Protection failed, printing logs.
		echo
		echo
		cat  peasoup*/logs/*
		echo
		echo
		echo Protection failed.  Logs were printed.
		exit 1
	fi
	exit 0
}

main "$@" 
