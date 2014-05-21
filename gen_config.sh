#!/bin/bash

######################################
# Generate an xml config from sample #
# by replacing all ${*} placeholders #
######################################

IFS='' # preserve whitespaces for read line

CURRENT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
TEMPLATE_PATH="$CURRENT_PATH/config.sample.xml"

cat $TEMPLATE_PATH |
while read line ; do
    while [[ "$line" =~ (\$\{[a-z_]+\}) ]] ; do
        LHS=${BASH_REMATCH[1]}
        RHS="$(eval echo "\"$LHS\"")"
        line=${line//$LHS/$RHS}
    done
    echo $line
done