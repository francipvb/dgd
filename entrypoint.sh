#!/bin/sh

set -e

BASE_DIR=$(cd `dirname $0` && pwd)
DGD_STOPPED=""
DGD_PID=""

function get_snapshots(){
	local SNAPSHOTS=""

	if [ -f "$BASE_DIR/state/snapshot" ]; then
		SNAPSHOTS="$BASE_DIR/state/snapshot"
	fi
	
	if [ -f "$BASE_DIR/state/snapshot.old" ]; then
		SNAPSHOTS="$SNAPSHOTS $BASE_DIR/state/snapshot.old"
	fi
	
	echo "$SNAPSHOTS"
}

function dgd_running() {
	if [ -z "$DGD_PID" ]; then
		return 1
	fi
	if kill -0 "$DGD_PID" 2&>/dev/null; then
		return 0
	fi
	
	return 1
}

function start_dgd(){
	local SNAPSHOTS=`get_snapshots`
	echo -n "Running DGD"
	if [ "$SNAPSHOTS" != "" ]; then
		echo -n " with snapshots $SNAPSHOTS"
	fi
	echo ""
	
	dgd "$BASE_DIR/cloud.dgd" $SNAPSHOTS &
	sleep 1
	DGD_PID=`pidof dgd`
}

function stop_dgd() {
	DGD_STOPPED="yes"
	if dgd_running; then
		echo "Stopping DGD..."
		kill -15 $DGD_PID
	fi
}

trap stop_dgd SIGINT SIGTERM

while [ "$DGD_STOPPED" != "yes" ]; do
	if ! dgd_running; then
		start_dgd
	fi
	sleep 1
done

echo "Exited"