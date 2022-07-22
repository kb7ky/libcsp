#!/bin/bash
# script to run a command and send output to logio
#
# first 3 args are server ground and radio
# rest of the args are run as command and options

server=$1
shift
ground=$1
shift
radio=$1
shift

cmd=$*

echo $server $ground $port "> $cmd <"

while true
do
    unbuffer $cmd | logio/sendto_logio.sh $server $ground $radio
    echo Restarting
done
