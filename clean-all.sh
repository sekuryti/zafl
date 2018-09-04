#!/bin/bash

cd $ZAFL_HOME

cd zipr_umbrella
./clean-all.sh

if [ -d zfuzz ]; then
	cd zfuzz
	./clean-all.sh
fi

# clean up installation if this module is the root of the install.
# skip pedi cleanup if we are part of a larger project, as future builds
# won't know how to install properly.  
if [[ $(head -1 manifest.txt.config) == $(pwd) ]] ; then 
	cd $ZAFL_HOME
	$PEDI_HOME/pedi -c -m manifest.txt
fi
