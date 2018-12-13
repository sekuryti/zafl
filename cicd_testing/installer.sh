#!/bin/bash
set -e
set -x

ZAFL_INSTALL_TEST_DIR=/tmp/zafl_install_test.$(whoami)/

cleanup()
{
	rm -fr $ZAFL_INSTALL_TEST_DIR
}

log_error()
{
	echo "Error: $1"
	exit 1
}

sanity_check_bc()
{
	$ZAFL_HOME/zfuzz/bin/zafl.sh $(which bc) bc.zafl
	if [ ! $? -eq 0 ]; then
		log_error "something went wrong trying to transform a binary with zafl instrumentation"
	fi

	if [ ! -x bc.zafl ]; then
		log_error "unable to produce a zafl-instrumented binary"
	fi

	# sanity check functionality
	echo "1+2" | bc > bc.orig.out
	echo "1+2" | ./bc.zafl > bc.zafl.out
	diff bc.orig.out bc.zafl.out
	if [ ! $? -eq 0 ]; then
		log_error "output of bc differs between bc and bc.zafl"
	fi
}

generate_release()
{
	cd $ZAFL_HOME/release
	echo "Generating releasing"
	./generate_release.sh

}

install_release()
{
	# re-install from installer
	if [ ! -d $ZAFL_INSTALL_TEST_DIR ]; then
		mkdir $ZAFL_INSTALL_TEST_DIR
	fi
	rm -fr $ZAFL_INSTALL_TEST_DIR/*
	cp $ZAFL_HOME/release/zafl_install.tgz $ZAFL_INSTALL_TEST_DIR

	pushd $ZAFL_INSTALL_TEST_DIR
	tar -xzvf zafl_install.tgz

	unset ZAFL_HOME
	unset PEASOUP_HOME
	unset LD_LIBRARY_PATH

	pushd zafl_install
	. set_env_vars

	echo $PSZ | grep zafl_install_test
		if [ ! $? -eq 0 ]; then
		log_error "env var PSZ does not point into installer directory"
	fi

	echo $ZAFL_HOME | grep zafl_install_test
	if [ ! $? -eq 0 ]; then
		log_error "env var ZAFL_HOME does not point into installer directory"
	fi
}

# generate and sanity check installer 
#cd /tmp/zafl_test
#source set_env_vars

generate_release
install_release

# sanity test using basic calculator
sanity_check_bc

cleanup




