#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# -*- mode: shell-script; indent-tabs-mode: nil; sh-basic-offset: 4; -*-
# ex: ts=8 sw=4 sts=4 et filetype=sh
set -eux
set -o pipefail

test "$(systemctl whoami)" = testsuite-23.service
test "$(systemctl whoami $$)" = testsuite-23.service

systemctl whoami 1 $$ 1 | cmp - /dev/fd/3 3<<'EOF'
init.scope
testsuite-23.service
init.scope
EOF
