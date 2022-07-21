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
FIFO=csp_output.log

cmd=$*

# create a fifo - removes regular file if it exists
if [ ! -p $FIFO ]
then
    rm -rf $FIFO
    mkfifo $FIFO
fi

echo $server $ground $port "> $cmd < using $FIFO"

while true
do
    (cat < $FIFO | logio/sendto_logio.sh $server $ground $radio) &
    $cmd
    echo Restarting
done
