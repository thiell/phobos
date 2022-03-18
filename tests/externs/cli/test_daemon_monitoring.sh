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

set -xe

test_dir=$(dirname $(readlink -e $0))
. $test_dir/../../test_env.sh
. $test_dir/../../setup_db.sh
. $test_dir/../../test_launch_daemon.sh

function test_drive_status_no_daemon()
{
    $phobos drive status &&
        error "LRS is required for drive status" ||
        true
}

function setup()
{
    setup_tables
    invoke_daemon
}

function cleanup()
{
    waive_daemon
    drop_tables
}

function test_drive_status()
{
    $phobos drive status | grep "drive status" ||
        error "Drive status failed"
}

test_drive_status_no_daemon

trap cleanup EXIT
setup

test_drive_status
