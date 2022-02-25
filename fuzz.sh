#!/bin/sh

mkdir -p "ARTIFACTS"
mkdir -p "CORPUS"
PARAMS="CORPUS/ music/ -artifact_prefix=ARTIFACTS/ -report_slow_units=2 -timeout=30"

COMMAND="$1"
shift

./"$COMMAND" $PARAMS "$@"
