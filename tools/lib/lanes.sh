#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/lanes.sh — the component lanes of `./tools/check fast`.
#
# Authority boundary: one lane per component that has host-runnable tests, each
# running that component's own documented commands
# (docs/development/testing-and-delivery.md section 6.2) and nothing invented
# here. The lane never reimplements a component's checks; it decides whether
# they can run on this host, runs them verbatim, and reports what happened.
#
# Owning milestone: M0 (WP-M0-09).
#
# Two rules every lane obeys:
#
#   1. A lane whose toolchain is absent SKIPS. A documentation-only host must
#      pass `./tools/check fast`, so a missing cargo is not a defect in the
#      change under review.
#   2. A lane never reports success for something it did not run. Every skip
#      names the reason and the exact next step; every pass names the command
#      that produced it.
#
# Consumed by two callers:
#   ./tools/check   sources everything and calls lane_run_<name>; it supplies
#                   step/pass/soft/hard/skip/note and $TMP.
#   ./tools/doctor  sources this file for the lane_ready_* predicates only, so
#                   the host audit and the gate can never disagree about which
#                   lanes this machine can run.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_LANES_SH_LOADED:-}" ]; then return 0; fi
TAFFY_LANES_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR source=versions.sh
. "$(dirname "${BASH_SOURCE[0]}")/versions.sh"
# shellcheck source-path=SCRIPTDIR source=android.sh
. "$(dirname "${BASH_SOURCE[0]}")/android.sh"
# shellcheck source-path=SCRIPTDIR source=node.sh
. "$(dirname "${BASH_SOURCE[0]}")/node.sh"

# Every lane of `./tools/check fast`, in execution order. The first six are
# repository-wide; the rest are the component lanes.
TAFFY_FAST_LANES='pins chromium files modules shell secrets architecture evidence catalog rust rust-graph contracts strings kotlin website fixtures fixtures-tasks icons mount orphans play public'

# The component lanes only — the ones whose ability to run depends on a
# toolchain, and therefore the ones ./tools/doctor reports on.
# shellcheck disable=SC2034  # read by ./tools/check and ./tools/doctor, not here
TAFFY_COMPONENT_LANES='architecture evidence catalog rust rust-graph contracts strings kotlin website fixtures fixtures-tasks icons mount orphans play public'

taffy_lane_known() { # <name>
  case " $TAFFY_FAST_LANES " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

# The lane predicates and the lane runners, split out when this file reached
# the 400-line cap. This file keeps what both halves need: the lane lists, the
# name lookup, and the shared setup above.
# shellcheck source-path=SCRIPTDIR source=lane_readiness.sh
. "$(dirname "${BASH_SOURCE[0]}")/lane_readiness.sh"
# shellcheck source-path=SCRIPTDIR source=lane_runners.sh
. "$(dirname "${BASH_SOURCE[0]}")/lane_runners.sh"
