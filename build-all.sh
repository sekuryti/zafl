#!/bin/bash

if [[ "$*" =~ "--debug" ]]; then
        SCONSDEBUG=" debug=1 "
        build_all_flags=" --debug "
fi

scons $SCONSDBUG -j 3
exit


