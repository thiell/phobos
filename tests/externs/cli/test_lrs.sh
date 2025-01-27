#!/bin/bash

#
#  All rights reserved (c) 2014-2022 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh
. $test_dir/../../utils_generation.sh
. $test_dir/../../tape_drive.sh
lrs_simple_client="$test_dir/lrs_simple_client"
pho_ldm_helper="$test_dir/../../../scripts/pho_ldm_helper"

set -xe

function setup
{
    export PHOBOS_LRS_lock_file="$test_bin_dir/phobosd.lock"
}

function cleanup
{
    drop_tables
    drain_all_drives
}

function test_invalid_lock_file()
{
    trap "waive_lrs" EXIT
    drop_tables
    setup_tables

set +e
    export PHOBOS_LRS_lock_file="/phobosd.lock"
    rm -rf "$PHOBOS_LRS_lock_file"
    invoke_lrs ||
        error "Should have succeeded with valid folder '/'"
    waive_lrs

    local folder="$test_bin_dir/a"
    export PHOBOS_LRS_lock_file="$folder/phobosd.lock"
    rm -rf "$folder"
    invoke_lrs &&
        error "Should have failed with non-existing folder '$folder'"

    mkdir -p "$folder"
    invoke_lrs ||
        error "Should have succeeded after creating valid folder '$folder'"
    waive_lrs

    rm -rf "$folder"

    # Create $folder as a simple file to fail the "is dir" condition
    touch "$folder"
    invoke_lrs &&
        error "Should have failed because '$folder' is not a directory"

    rm -rf "$folder"
set -e

    unset PHOBOS_LRS_lock_file
    drop_tables

    trap cleanup EXIT
}

function test_multiple_instances
{
    setup_tables

    pidfile="/tmp/pidfile"

    $phobosd -i &
    first_process=$!

    sleep 1

    timeout 60 $LOG_COMPILER $phobosd -i &
    second_process=$!

    wait $second_process && true
    rc=$?
    kill $first_process

    # Second daemon error code should be EEXIST, which is 17
    test $rc -eq 17 ||
        error "Second daemon instance does not get the right error code"

    drop_tables
}

function test_recover_dir_old_locks
{
    setup_tables

    dir0=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    dir1=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    dir2=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    dir3=$(mktemp -d /tmp/test_recover_dir_old_locksXXX)
    $phobos dir add --unlock ${dir0} ${dir1} ${dir2} ${dir3}

    host=$(hostname)
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    $PSQL -c \
        "insert into lock (type, id, hostname, owner) values
             ('media'::lock_type, '${dir0}', '$host', $pid),
             ('media_update'::lock_type, '${dir1}', '$host', $pid),
             ('media'::lock_type, '${dir2}', '${host}other', $pid),
             ('media_update'::lock_type, '${dir3}', '${host}other', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -vv -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0} ${dir1} ${dir2} ${dir3}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"

    lock=$($phobos dir list -o lock_hostname ${dir1})
    [ "None" == "$lock" ] || error "${dir1} should be unlocked"

    lock=$($phobos dir list -o lock_hostname ${dir2})
    [ "${host}other" == "$lock" ] || error "${dir2} should be locked"

    lock=$($PSQL -t -c "select hostname from lock where id = '${dir3}';" |
           xargs)
    [ "${host}other" == "$lock" ] || error "${dir3} should be locked"

    drop_tables
}

function test_remove_invalid_media_locks
{
    setup_tables

    dir0=$(mktemp -d /tmp/test_remove_invalid_media_locksXXX)
    dir1=$(mktemp -d /tmp/test_remove_invalid_media_locksXXX)

    host=$(hostname)
    pid=$BASHPID

    # Update media to lock them by a 'daemon instance'
    # Only one is locked by this host
    $PSQL -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('dir', NULL, 'blob:${dir0}', 'blob', 'unlocked', '${dir0}'),
                   ('dir', NULL, 'blob:${dir1}', 'blob', 'unlocked',
                    '${dir1}');"
    $PSQL -c \
        "insert into media (family, model, id, adm_status, fs_type,
                            address_type, fs_status, stats, tags)
            values ('dir', NULL, '${dir0}', 'unlocked', 'POSIX', 'HASH1',
                    'blank', '{\"nb_obj\":0, \"logc_spc_used\":0, \
                               \"phys_spc_used\":0, \"phys_spc_free\":1024, \
                               \"nb_load\":0, \"nb_errors\":0, \
                               \"last_load\":0}', '[]'),
                   ('dir', NULL, '${dir1}', 'unlocked', 'POSIX', 'HASH1',
                    'blank', '{\"nb_obj\":0, \"logc_spc_used\":0, \
                               \"phys_spc_used\":0, \"phys_spc_free\":1024, \
                               \"nb_load\":0, \"nb_errors\":0, \
                               \"last_load\":0}', '[]');"
    $PSQL -c \
        "insert into lock (type, id, hostname, owner)
            values ('media'::lock_type, '${dir0}', '$host', $pid),
                   ('media_update'::lock_type, '${dir1}', '$host', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="dir" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?
    rmdir ${dir0} ${dir1}

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the locks of the correct hostname are released
    lock=$($phobos dir list -o lock_hostname ${dir0})
    [ "None" == "$lock" ] || error "${dir0} should be unlocked"
    lock=$($PSQL -t -c "select hostname from lock where id = '${dir1}';" |
           xargs)
    [ -z $lock ] || error "${dir1} should be unlocked"

    drop_tables
}

function test_recover_drive_old_locks
{
    setup_tables

    $phobos drive add --unlock /dev/st[0-1]

    host=`hostname`
    pid=$BASHPID

    # Inserting directly into the lock table requires the
    # actual names of each drive, so we fetch them
    dev_st0_id=$($phobos drive list -o name /dev/st0)
    dev_st1_id=$($phobos drive list -o name /dev/st1)

    # Update devices to lock them by a 'daemon instance'
    # Only one is locked by this host
    $PSQL -c \
        "insert into lock (type, id, hostname, owner) values
             ('device'::lock_type, '$dev_st0_id', '$host', $pid),
             ('device'::lock_type, '$dev_st1_id', '${host}other', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="tape" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check that only the correct device is unlocked
    lock=$($phobos drive list -o lock_hostname /dev/st0)
    [ "None" == "$lock" ] || error "Device should be unlocked"

    lock=$($phobos drive list -o lock_hostname /dev/st1)
    [ "${host}other" == "$lock" ] || error "Device should be locked"

    drop_tables
}

function test_remove_invalid_device_locks
{
    setup_tables

    $phobos drive add --unlock /dev/st0

    host=`hostname`
    pid=$BASHPID
    fake_host="blob"

    dev_st0_id=$($phobos drive list -o name /dev/st0)
    dev_st1_model=$($phobos drive list -o model /dev/st0)
    dev_st1_id="fake_id_remove_invalid_device_locks"

    $PSQL -c \
        "insert into device (family, model, id, host, adm_status, path)
            values ('tape', '$dev_st1_model', '$dev_st1_id', '$fake_host',
                    'unlocked', '/dev/st1');"
    $PSQL -c \
        "insert into lock (type, id, hostname, owner)
            values ('device'::lock_type, '$dev_st1_id', '$host', $pid);"

    # Start and stop the lrs daemon
    PHOBOS_LRS_families="tape" timeout --preserve-status 10 $phobosd -i &
    daemon_process=$!

    wait $daemon_process && true
    rc=$?

    # check return status
    test $rc -eq 0 ||
        error "Daemon process returns an error status : ${rc}"

    # Check only the lock of the correct hostname is released
    lock=$($phobos drive list -o lock_hostname /dev/st0)
    [ "None" == "$lock" ] || error "Dir should be unlocked"

    lock=$($phobos drive list -o lock_hostname /dev/st1)
    [ "None" == "$lock" ] || error "Dir should be unlocked"

    drop_tables
}

function test_wait_end_of_IO_before_shutdown()
{
    local dir=$(mktemp -d)

    trap "waive_lrs; drop_tables; rm -rf '$dir'" EXIT
    setup_tables
    invoke_lrs -vv

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    local release_medium_name=$($lrs_simple_client put dir)

    kill $PID_LRS
    sleep 1
    ps --pid $PID_LRS || error "Daemon should still be online"

    # send release request
    $lrs_simple_client release $release_medium_name dir

    timeout 10 tail --pid=$PID_LRS -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_LRS=0

    drop_tables
    rm -rf '$dir'
}

function wait_for_process_end()
{
    local pid="$1"
    local count=0

    while ps --pid "$pid"; do
        if (( count > 10 )); then
            error "Process $pid should have stopped after 10s"
        fi
        ((count++)) || true
        sleep 1
    done
}

function test_cancel_waiting_requests_before_shutdown()
{
    local dir=$(mktemp -d)
    local file=$(mktemp)
    local res_file="res_file"

    trap "waive_lrs; drop_tables; rm -rf '$dir' '$file' '$res_file'" EXIT
    setup_tables
    invoke_lrs

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    local release_medium_name=$($lrs_simple_client put dir)

    # this request will be waiting in the LRS as the only dir is used by
    # lrs_simple_client
    ( set +e; $phobos put --family dir "$file" oid; echo $? > "$res_file" ) &
    local put_pid=$!

    # wait for the request to reach the LRS
    sleep 1

    kill $PID_LRS

    # "timeout wait $put_pid" cannot be used here as "put_pid" will not be a
    # child of 'timeout'
    wait_for_process_end $put_pid

    if [[ $(cat "$res_file") == 0 ]]; then
        error "Waiting request should have been canceled"
    fi

    # send the release request
    $lrs_simple_client release $release_medium_name dir

    timeout 10 tail --pid=$PID_LRS -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_LRS=0

    drop_tables
    rm -rf "$dir" "$file" "$res_file"
}

function test_refuse_new_request_during_shutdown()
{
    local dir=$(mktemp -d)
    local file=$(mktemp)

    trap "waive_lrs; drop_tables; rm -rf '$dir' '$file'" EXIT
    setup_tables
    invoke_lrs

    $phobos dir add "$dir"
    $phobos dir format --unlock --fs posix "$dir"

    local release_medium_name=$($lrs_simple_client put dir)

    kill $PID_LRS

    $phobos put --family dir "$file" oid &&
        error "New put should have failed during shutdown"

    # send the release request
    $lrs_simple_client release $release_medium_name dir

    timeout 10 tail --pid=$PID_LRS -f /dev/null
    if [[ $? != 0 ]]; then
        error "Daemon not stopped after 10 seconds"
    fi
    PID_LRS=0

    drop_tables
    rm -rf "$dir" "$file"
}

function test_mount_failure_during_read_response()
{
    local file=$(mktemp)
    local tape=$(get_tapes L6 1)
    local drive=$(get_lto_drives 6 1)

    trap "waive_lrs; drop_tables; rm -f '$file'; \
          unset PHOBOS_LTFS_cmd_mount" EXIT
    setup_tables
    invoke_lrs

    dd if=/dev/urandom of="$file" bs=4096 count=5

    $phobos tape add --type lto6 "$tape"
    $phobos drive add "$drive"
    $phobos drive unlock "$drive"
    $phobos tape format --unlock "$tape"

    $phobos put "$file" oid ||
        error "Put command failed"

    # Force mount to fail
    local save_mount_cmd=$PHOBOS_LTFS_cmd_mount
    export PHOBOS_LTFS_cmd_mount="sh -c 'exit 1'"
    waive_lrs
    invoke_lrs

    $phobos get oid "${file}.out" &&
        error "Get command should have failed"

    ps --pid "$PID_LRS"

    export PHOBOS_LTFS_cmd_mount="$save_mount_cmd"
    waive_lrs
    drop_tables

    rm -f "$file"
}

function format_wait_and_check()
{
    local format_pid
    local ENODEV=19
    local rc

    $phobos tape format --unlock "$1" &
    format_pid=$!

    sleep 3

    kill -9 $format_pid && true

    wait $format_pid && true
    rc=$?

    test $rc -eq $ENODEV ||
        error "Format command should have failed because $2" \
              "(error received $rc, expected $ENODEV 'ENODEV')"
}

function test_format_fail_without_suitable_device()
{
    local drive=$(get_lto_drives 5 1)
    local tape=$(get_tapes L6 1)

    trap "waive_lrs; drop_tables" EXIT

    setup_tables
    invoke_lrs

    $phobos tape add --type lto6 "$tape"
    format_wait_and_check "$tape" "no device is available"

    $phobos drive add "$drive"
    format_wait_and_check "$tape" "no device thread is running"

    $phobos drive unlock "$drive"
    format_wait_and_check "$tape" \
                          "the drive '$drive' and tape '$tape' are incompatible"

    waive_lrs
    drop_tables

    trap "cleanup" EXIT
}

function test_retry_on_error_setup()
{
    drain_all_drives
    drop_tables
    setup_tables
    invoke_lrs

    setup_test_dirs
    setup_dummy_files 2
}

function test_retry_on_error_cleanup()
{
    waive_lrs
    drain_all_drives

    cleanup_dummy_files
    cleanup_test_dirs
    rm -f /tmp/mount_count

    drop_tables
}

function test_retry_on_error_run()
{
    local drives=$(get_lto_drives 6 3)
    local tapes=$(get_tapes L6 3)
    local file=${FILES[1]}
    local oid=$(basename "$file")

    $phobos drive add --unlock $drives

    $phobos tape add --type lto6 "$tapes"
    $phobos tape format --unlock "$tapes"

    $phobos put --layout raid1 --lyt-params "repl_count=3" "$file" "$oid"

    waive_lrs
    drain_all_drives

    # Custom mount script that fails the first two mounts and succeeds on the
    # third attempt.
    local cmd="bash -c \"
mount_count=\$(cat /tmp/mount_count)
echo mount count: \$mount_count

if (( mount_count == 2 )); then
    $pho_ldm_helper mount_ltfs '%s' '%s'
    exit
fi
((mount_count++))
echo \$mount_count > /tmp/mount_count
exit 1
\""

    echo 0 > /tmp/mount_count
    local save_mount_cmd=$PHOBOS_LTFS_cmd_mount
    export PHOBOS_LTFS_cmd_mount="$cmd"
    invoke_lrs
    $phobos ping phobosd

    $phobos get "$oid" "$DIR_TEST_OUT"/"$oid"
    export PHOBOS_LTFS_cmd_mount="$save_mount_cmd"
}

function test_retry_on_error()
{
    for algo in fifo grouped_read; do
        (
         # This test is only executed for tapes
         export PHOBOS_IO_SCHED_TAPE_read_algo="$algo"
         set -xe

         trap test_retry_on_error_cleanup EXIT
         test_retry_on_error_setup
         test_retry_on_error_run

         unset PHOBOS_IO_SCHED_TAPE_read_algo
        )
    done
}

function test_fair_share_max_reached()
{
    local drive=$(get_lto_drives 5 1)
    local tape=$(get_tapes L5 1)
    local oid=test_fair_share_max_reached

    drop_tables
    setup_tables
    export PHOBOS_IO_SCHED_TAPE_dispatch_algo=fair_share
    trap "waive_lrs; cleanup" EXIT
    invoke_lrs

    # With this setup, any get will wait
    $phobos sched fair_share --type LTO5 --min 0,0,0 --max 0,1,1

    $phobos drive add --unlock $drive
    $phobos tape add --type lto5 "$tape"
    $phobos tape format --unlock "$tape"

    $phobos put /etc/hosts $oid

    local lock_hostname=$($phobos tape list -o lock_hostname $tape)
    rm -f /tmp/$oid
    $phobos get $oid /tmp/$oid &
    local pid=$!
    sleep 1

    local new_lock_hostname=$($phobos tape list -o lock_hostname $tape)
    if [ "$lock_hostname" != "$new_lock_hostname" ]; then
        # make sure that we don't unlock the medium when trying to alloc the
        # read request and we don't have any device available.
        error "Lock has been changed! Previous hostname: $lock_hostname," \
            "Current hostname: $new_lock_hostname"
    fi

    ps $pid || error "phobos get process is not running"
    $phobos sched fair_share --type LTO5 --max 1,1,1
    wait || error "Get should have succeeded after setting max reads to 1"
    waive_lrs
    trap cleanup EXIT
}

function test_no_DAEMON_PID_FILEPATH_lock_cleaned()
{
    setup_tables

    # Start LRS daemon without DAEMON_PID_FILEPATH
set +e
    $phobosd -v
    rc=$?
set -e

    # Check that daemon starts fails returning EXIT_FAILURE == 1
    if (( rc != 1 )); then
        if (( rc == 0 )); then
           pkill phobosd
        fi

        error "Daemon starts must return 1 with no DAEMON_PID_FILEPATH"
    fi

    # Wait (max 5s) for child end
    for i in `seq 5`; do
        if pgrep phobosd; then
            sleep 1
        else
            break
        fi
    done

    if [[ "${i}" == "5" ]]; then
        if pkill phobosd; then
            error "Child must fails when father has no DAEMON_PID_FILEPATH"
        fi
    fi

    # Check lock file is cleaned
    if [[ -f ${PHOBOS_LRS_lock_file} ]]; then
        error "Lock file must be cleared when daemon start fails"
    fi

    drop_tables
}

trap cleanup EXIT

test_invalid_lock_file

setup

test_multiple_instances
test_recover_dir_old_locks
test_remove_invalid_media_locks
test_wait_end_of_IO_before_shutdown
test_cancel_waiting_requests_before_shutdown
test_refuse_new_request_during_shutdown
test_no_DAEMON_PID_FILEPATH_lock_cleaned

# Tape tests are available only if /dev/changer exists, which is the entry
# point for the tape library.
if [[ -w /dev/changer ]]; then
    test_retry_on_error
    test_recover_drive_old_locks
    test_remove_invalid_device_locks
    test_mount_failure_during_read_response
    test_format_fail_without_suitable_device
    test_fair_share_max_reached
fi
