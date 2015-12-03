#!/bin/bash
#
# Dumps the last open USB tty serial port while being friendly with
# others programs that also require the port (e.g. avrdude upload).
# Just send USR1 to stop and USR2 to resume (pid is in /tmp/auto_serial.pid)
# Warning: it will send the serial data back to the incoming stream (bug to fix!)
#

# Run this script in its own folder (hardcore bashism)
DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd $DIR

baudRate=${1-115200}
resumeNow=
socatPid=

function trapINT()
{
	clear
	echo
	echo "$(date +'%Y%m%d-%H%M%S') - Received INT/USR1"
	test -n "$socatPid" && kill $socatPid
	rm /tmp/auto_serial.pid
    exec $0 "$@"
    exit 0
}
function trapUSR2()
{
	echo
	echo "$(date +'%Y%m%d-%H%M%S') - Received USR2"
	resumeNow=1
}

trap trapINT  USR1
trap trapUSR2 USR2

echo $$ > /tmp/auto_serial.pid
while true; do
	echo "Press <enter> to start listening (or USR1 signal), or <ctrl-C> to quit now."
	resumeNow=
	while [[ -z $resumeNow ]]; do
		echo -n '.'
		read -t 0.5 && break # manual start is possible, else use USR1
	done
	
	echo
	trap trapINT INT
	
	tty=$(dmesg|egrep 'tty(USB|ACM)'|tail -1|sed 's|.*tty\([ACMUSB][ACMUSB]*[0-9][0-9]*\)|/dev/tty\1|')

	## /bin/stty -F $tyy --> would be cool but it would work only interactively for me
	if [[ -c $tty ]]; then
		echo "READING FROM $tty (ctrl-C to stop):"
		nice socat ${tty},raw,echo=1,crnl,b${baudRate} /dev/stdout &
		socatPid=$!
		while true; do true; done
	fi

done
