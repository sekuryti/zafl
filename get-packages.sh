#!/bin/bash 

args="$@"
if [[ $args = "" ]]; then
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

pushd zipr_umbrella
sudo -E ./get-peasoup-packages.sh $args
popd

pushd zfuzz
sudo -E ./get-packages.sh $args
popd
