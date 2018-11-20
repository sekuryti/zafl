PKGS="libtool glib2.0-dev texinfo libtool-bin"

for i in $PKGS
do
	sudo apt-get install $i -y --force-yes
done
