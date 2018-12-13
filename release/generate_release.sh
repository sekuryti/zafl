#/bin/bash

# use absolute paths
releasedir=$(dirname $(readlink -f $0))
zaflinstalldir=$releasedir/zafl_install
zestruntimeinstalldir=$releasedir/zafl_install/zest_runtime

pre_cleanup()
{
	pushd $releasedir
	rm *.tgz >/dev/null 2>&1
	rm -fr $zaflinstalldir >/dev/null 2>&1
	popd
}

post_cleanup()
{
	pushd $releasedir
	rm -fr $zaflinstalldir >/dev/null 2>&1
	popd
}

main()
{
	# create local copy of install dir
	pre_cleanup
	mkdir $zaflinstalldir

	# regen install for zipr umbrella
	cd $PEASOUP_UMBRELLA_DIR
	echo "Reinstalling just the right parts." 
	./regen_install.sh ps zipr stars >/dev/null 
	echo "Adding zipr sub-component"
	cp -r $ZAFL_HOME/install/zipr_umbrella $zaflinstalldir
	$PEDI_HOME/pedi -c -m manifest.txt > /dev/null 

	echo "Adding installation README.txt"
	cp $releasedir/README.txt $zaflinstalldir

	echo "Adding zest/zafl runtime libraries"
	mkdir -p $zestruntimeinstalldir/lib
	cd $zestruntimeinstalldir/lib
	cp $PEASOUP_UMBRELLA_DIR/zest_runtime/lib64/*.so .

	cd $releasedir
	echo "Adding zfuzz sub-component"
	cp -r $ZAFL_HOME/install/zfuzz $zaflinstalldir
	cp -r $ZAFL_HOME/install/set_env_vars $zaflinstalldir
	echo "Creating installation archive"
	tar czf zafl.tgz zafl_install

	echo "Cleaning up"
	post_cleanup

	echo "Complete: release tarball ready"
}

main "$@"
