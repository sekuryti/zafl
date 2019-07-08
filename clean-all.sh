#!/bin/bash

cd $ZAFL_HOME/zipr_umbrella
./clean-all.sh

cd $ZAFL_HOME/libzafl
scons -c

cd $ZAFL_HOME/tools
scons -c


# clean up installation if this module is the root of the install.
# skip pedi cleanup if we are part of a larger project, as future builds
# won't know how to install properly.  
cd $ZAFL_HOME
if [[ $(head -1 manifest.txt.config) == $(pwd) ]] ; then 
	$PEDI_HOME/pedi -c -m manifest.txt
fi
