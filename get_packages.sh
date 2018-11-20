#!/bin/bash 

pushd zipr_umbrella
sudo -E ./get-peasoup-packages.sh all
popd

pushd zfuzz
sudo -E ./get-packages.sh
popd
