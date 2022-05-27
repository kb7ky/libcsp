#!/bin/bash
# Script to send to log.io server
USAGE="$0: -s server -g station -r radio"

if [ $# != 3 ]
then
    echo $USAGE
    exit 1
fi

# defines
port=6689

#assign args
server=$1
station=$2
radio=$3

echo Starting log to $server:6689 for $station - $radio

# init stream
echo -e "-input|${station}|${radio}\0" | nc $server $port
echo -e "+input|${station}|${radio}\0" | nc $server $port

# main loop
while read line
do
    echo $line
    echo -e "+msg|${station}|${radio}|${line}\0" | nc $server $port
done

echo -e "+msg|${station}|${radio}|logio exiting\0" | nc $server $port