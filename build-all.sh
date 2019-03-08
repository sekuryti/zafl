#!/bin/bash

if [[ "$*" =~ "--debug" ]]; then
        SCONSDEBUG=" debug=1 "
        build_all_flags=" --debug "
fi


# check if DIR is the directory containing the build script.
BUILD_LOC=`dirname $0`
FULL_BUILD_LOC=`cd $BUILD_LOC; pwd`
if [ "$ZAFL_HOME" != "$FULL_BUILD_LOC" ]; then
    echo "ZAFL_HOME ($ZAFL_HOME) differs from build-all.sh location ($FULL_BUILD_LOC).";
    echo "Did you source set_env_vars from the root of the umbrella working copy?";
    exit 1;
fi

if [ ! -f manifest.txt.config -o ! -d "$ZAFL_INSTALL" ]; then
	mkdir -p "$ZAFL_INSTALL"
        $PEDI_HOME/pedi --setup -m manifest.txt -l zafl -l ps -l zipr -l stratafier -l stars -i $ZAFL_INSTALL || exit
fi

cd zipr_umbrella
./build-all.sh $build_all_flags  || exit


cd $ZAFL_HOME
cd tools
scons $SCONSDEBUG -j 3

cd $ZAFL_HOME
$PEDI_HOME/pedi -m manifest.txt || exit

cd $ZAFL_HOME/libzafl
scons
cp lib/* $ZEST_RUNTIME/lib64/

cd $ZAFL_HOME
echo "ZAFL Overall build complete."



