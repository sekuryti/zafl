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

# @todo: none of these are necessary?
#        these were for building QEMU/afl but that's not a zfuzz concern
which apt-get >/dev/null 2>/dev/null
if [ $? -eq 0 ]; then
	PKGS="libtool glib2.0-dev texinfo libtool-bin"
else
	PKGS="libtool glib2.0-dev texinfo"
fi

for i in $PKGS
do
	which apt-get >/dev/null 2>/dev/null
	if [ $? -eq 0 ]; then
		sudo apt-get install $i -y --force-yes
	else
		sudo yum install $i -y --skip-broken
	fi
done
