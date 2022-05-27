#!/bin/bash
# Script to send to log.io server
USAGE="$0: server station radio"

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

# init stream
echo -e "-input|${station}|${radio}\0" | nc -q 0 $server $port
echo -e "+input|${station}|${radio}\0" | nc -q 0 $server $port

echo Starting log to $server:6689 for $station - $radio

# main loop
while read line
do
    echo $line
    echo -e "+msg|${station}|${radio}|${line}\0" | nc -q 0 $server $port
done

echo -e "+msg|${station}|${radio}|logio exiting\0" | nc -q 0 $server $port