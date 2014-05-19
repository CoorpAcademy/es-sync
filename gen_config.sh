#!/bin/bash

######################################
# Generate an xml config from sample #
# by replacing all ${*} placeholders #
######################################

IFS='' # preserve whitespaces for read line

cat config.sample.xml |
while read line ; do
    while [[ "$line" =~ (\$\{[a-z_]+\}) ]] ; do
        LHS=${BASH_REMATCH[1]}
        RHS="$(eval echo "\"$LHS\"")"
        line=${line//$LHS/$RHS}
    done
    echo $line
done