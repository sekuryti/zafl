PKGS="libtool glib2.0-dev"

for i in $PKGS
do
	sudo apt-get install $i -y --force-yes
done
