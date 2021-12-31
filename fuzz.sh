#!/bin/sh

mkdir -p "ARTIFACTS"
mkdir -p "CORPUS"
PARAMS="CORPUS music/ -artifact_prefix=ARTIFACTS/ -timeout=30"

COMMAND="$1"
shift

./"$COMMAND" $PARAMS "$@"
