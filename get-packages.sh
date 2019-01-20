#!/bin/bash

#
# skeleton get-packages.sh
#

args="$@"
if [[ "$args" == "" ]]; then
        args="all"
fi

for arg in $args; do
	case $arg in
		all | build | test | deploy)
			;;
		*)
		echo "arg not recognized. Recognized args: all, build, test, deploy"
		exit 1
		;;
	esac
done
