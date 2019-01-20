#
# skeleton get-packages.sh
#

#
# do not need any additional packages above and beyond Zipr toolchain
# those packages will be installed separately
#

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
