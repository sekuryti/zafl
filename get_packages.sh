#!/bin/bash 

pushd zipr_umbrella
sudo -E ./get-peasoup-packages.sh all
popd

pushd zfuzz
./get-packages.sh
popd
