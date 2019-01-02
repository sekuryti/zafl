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
