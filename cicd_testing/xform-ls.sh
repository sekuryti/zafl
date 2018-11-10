#!/bin/bash
set -e
set -x

cd /tmp/zafl_test
source set_env_vars
cd /tmp
rm -rf xxx ped_ls; $PSZ /bin/ls ./xxx -c rida=on -s meds_static=off --tempdir ped_ls || true
if [[ ! -x ./xxx ]]; then cat ped_ls/logs/*; fi
rm -rf ped_ls
./xxx

