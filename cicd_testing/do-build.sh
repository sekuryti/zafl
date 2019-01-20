#!/bin/bash
set -e
set -x


# update submodules
git submodule sync --recursive
git submodule update --recursive --init
# gather info for debugging later, probably not necessary 
pwd
hostname
whoami
env|grep CICD

time rsync -a --exclude='.git'  $CICD_TO_TEST_DIR/ /tmp/zafl_test
cd /tmp/zafl_test
source set_env_vars
sudo ./get_packages.sh all
./build-all.sh 
cd $PEASOUP_UMBRELLA_DIR
./postgres_setup.sh
