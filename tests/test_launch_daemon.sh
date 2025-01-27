#!/bin/sh

PID_LRS=0
PID_TLC=0

LRS_PID_FILEPATH="$test_bin_dir/phobosd.pid"
export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"

TLC_PID_FILEPATH="$test_bin_dir/tlc.pid"

function invoke_daemon()
{
    local PID_FILEPATH=$1
    local daemon_bin=$2
    local PID_VAR_NAME=$3
    shift 3

    DAEMON_PID_FILEPATH=${PID_FILEPATH} $LOG_COMPILER $LOG_FLAGS \
        ${daemon_bin} "$*" &
    wait $! || { echo "failed to start ${daemon_bin}"; return 1; }
    eval ${PID_VAR_NAME}=`cat ${PID_FILEPATH}`
}

function invoke_lrs()
{
   invoke_daemon ${LRS_PID_FILEPATH} $phobosd PID_LRS "$*"
}

function invoke_tlc()
{
   invoke_daemon ${TLC_PID_FILEPATH} $tlc PID_TLC "$*"
}

function waive_daemon()
{
    local PID_FILEPATH=$1
    local PID_VAR_NAME=$2

    if [ ! -f "${PID_FILEPATH}" ]; then
        return 0
    fi

    eval ${PID_VAR_NAME}=`cat ${PID_FILEPATH}`
    rm ${PID_FILEPATH}

    if [ ${!PID_VAR_NAME} -eq 0 ]; then
        return 0
    fi

    kill ${!PID_VAR_NAME} &>/dev/null || \
        echo "Daemon ${!PID_VAR_NAME} was not running"
    # wait would not work here because ${!PID_VAR_NAME} is not an actual child
    # of this shell (created in invoke_daemon)
    timeout 10 tail --pid=${!PID_VAR_NAME} -f /dev/null
    eval ${PID_VAR_NAME}=0
}

function waive_lrs()
{
    waive_daemon ${LRS_PID_FILEPATH} PID_LRS
}

function waive_tlc()
{
    waive_daemon ${TLC_PID_FILEPATH} PID_TLC
}
