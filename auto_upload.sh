#!/bin/bash
set -e

MORE_AVR_OPTS="-v"

# First argument shall be "uno", "mega" or "auto"
DEVICE=${1-auto}
# Second argument shall be the .hex file
hex=${2-*.hex}
[[ ! -f $hex ]] && hex=$(find -iname $hex|head -1)
[[ ! -f $hex ]] && echo "Hex file \"$hex\" not found, sorry" && exit 1

# Run companion script in a detached windows if it is not already running
pgrep -lf 'sh\s*.*/auto_serial.sh' || nohup konsole -e $(dirname $0)/auto_serial.sh >/dev/null 2>&1 &

function remoteCtl
{
	# send the remote signal to start
	if [[ -f /tmp/auto_serial.pid ]]; then
		remotePid=$(cat /tmp/auto_serial.pid)
		echo "kill -SIG$1 $remotePid" | true
		kill -SIG$1 $remotePid | true
	fi
}

# stop the remote serial watcher
remoteCtl USR1

# Get the last USB tty from the system log
tty=$(dmesg|egrep 'tty(USB|ACM)'|tail -1|sed 's|.*tty\([ACMUSB][ACMUSB]*[0-9][0-9]*\)|/dev/tty\1|')
#[[ ! -c $tty ]] && echo "ttyACMx not found, sorry" && exit 2

# Auto-detect the AVR if needed
signature=
if [[ $DEVICE = "auto" ]]; then
	echo "Detecting device"
	signature=$(avrdude -carduino -p ATtiny2313 -P${tty} 2>&1 |sed -n 's/.*Device signature = //p')
	if [[ $signature = "0x1e950f" ]]; then
		DEVICE="uno"
	fi
fi
if [[ $DEVICE = "mega" ]]; then
	AVR_OPTS="-pm2560 -cstk500v2 -D"
	KIND='a'
elif [[ $DEVICE = "uno" ]]; then
	AVR_OPTS="-patmega328p -carduino -D"
	KIND='i'
else
	echo "Unsupported AVR device $signature (only Mega and Uno are for now)"
	false
fi

echo "Uploading $hex to $tty"
killall avrdude || true
cmd="/usr/bin/avrdude $MORE_AVR_OPTS $AVR_OPTS -P${tty} -Uflash:w:${hex}:${KIND}"
echo $cmd
$cmd


# resume the remote serial watcher
remoteCtl USR2
