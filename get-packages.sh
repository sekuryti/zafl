#!/bin/bash 

args="$@"
if [[ $args = "" ]]; then
        args="all"
fi

for arg in $args; do
	case $arg in
		all | deploy | test)
                        which apt-get 1> /dev/null 2> /dev/null
                        if [ $? -eq 0 ]; then
                                sudo apt-get install -y --ignore-missing afl clang
                        else
                                sudo yum install -y --skip-broken afl clang
                        fi
                ;;

		build)
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

