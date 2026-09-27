#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/fork_debt.sh — the fork-debt gate of `./tools/check fast`.
#
# Authority boundary: this module owns *how downstream debt against Chromium is
# counted, judged and reported*. It owns neither the budget — decision 0013 and
# docs/development/chromium-fork-and-build.md section 1.3 set it — nor the work
# being counted, which is chromium/patches/. Every per-change figure it prints
# is read out of that directory at run time, so no estimate is written down in
# two places and no total can drift from the specifications it sums.
#
# Owning milestone: M0 (WP-M0-09).
#
# Two figures, because either one alone misleads:
#
#   realised   the .patch files that exist, and the upstream lines they modify.
#              This is what a build applies and the only figure that can block
#              one. It is no longer zero: most of the queue is exported and
#              applied against the pinned milestone, and the figure is now a
#              measurement of a real fork delta rather than a fact about an
#              empty queue. Nothing here restates it — run the gate. The
#              zero-patch branch below stays, because a queue can empty again
#              (every patch upstreamed or refactored into //taffy)
#              and the two states must not print the same sentence.
#   projected  realised plus every numbered specification not yet exported, each
#              at the estimate it records for itself. This is what the queue
#              holds once the specified work lands, and it is the figure that
#              can still be steered cheaply: an upstream edit is refactored into
#              //taffy for the cost of an argument before it is
#              written, and for the cost of a rebase every milestone after.
#
# A specification whose .patch sibling exists is counted as realised and never
# as projected, so exporting a patch moves a change between the two figures
# instead of double-counting it.
#
# A specification the queue has marked **retired** is counted as neither. That
# is the whole of what "retire a specification" buys, and until it was read here
# the projection charged the fork for two edits it had already decided not to
# make while the gate's own remedy line told the reader to retire something.
#
# Sourced by ./tools/check after its reporting helpers (step/pass/soft/hard/
# note) exist. Never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_FORK_DEBT_SH_LOADED:-}" ]; then return 0; fi
TAFFY_FORK_DEBT_SH_LOADED=1

# The executable copies of the two enforceable bounds in
# docs/development/chromium-fork-and-build.md section 1.3. That document is the
# authority; these are the only copies anywhere in the tools. The third bound it
# states — a milestone rebase inside two engineer-days — is produced by an
# actual rebase, not by a read-only gate, and has never been measured.
TAFFY_FORK_MAX_PATCHES=40
TAFFY_FORK_MAX_LINES=1500

# The queue, relative to the repository root. `security/` holds urgent
# cherry-picks and counts toward the same bounds: a line is downstream debt
# whether it arrived at a milestone rebase or at 3am.
TAFFY_FORK_QUEUE='chromium/patches'

# --- reading the queue ------------------------------------------------------

# Where the warning band starts for a bound: 80% of it. A figure inside the band
# still passes — the warning is the last moment at which shedding debt is cheap.
taffy_fork_warn_floor() { # <bound>
  printf '%d\n' $(( $1 * 4 / 5 ))
}

taffy_fork_commas() { # <integer>
  printf '%s\n' "$1" | sed -e :a -e 's/\(.*[0-9]\)\([0-9]\{3\}\)/\1,\2/;ta'
}

# Every exported patch: the milestone queue first, then the security
# cherry-picks, matching the order ./tools/chromium/sync applies them in.
taffy_fork_patch_files() {
  find "$TAFFY_ROOT/$TAFFY_FORK_QUEUE" -name '*.patch' -type f 2>/dev/null | sort
}

# Modified upstream lines across the exported queue. A diff body line counts
# once; the ---/+++ file headers do not, which is why the pattern rejects a
# second leading +/-.
taffy_fork_patched_lines() {
  local files
  files=$(taffy_fork_patch_files)
  if [ -z "$files" ]; then printf '0\n'; return 0; fi
  printf '%s\n' "$files" | tr '\n' '\0' | xargs -0 cat 2>/dev/null \
    | grep -c -E '^[+-]([^+-]|$)' || true
}

# The numbered specifications — one per logical change, whether or not it is a
# diff yet. Top level only: chromium/patches/security/ carries no specification,
# because an urgent cherry-pick is an upstream commit rather than a design.
taffy_fork_spec_files() {
  find "$TAFFY_ROOT/$TAFFY_FORK_QUEUE" -maxdepth 1 -type f \
    -name '[0-9][0-9][0-9][0-9]-*.md' 2>/dev/null | sort
}

# 0 when this specification has already become a patch. A number names one
# logical change for its whole life, so the patch is found by number and not by
# the rest of the filename, which may legitimately be reworded.
taffy_fork_spec_exported() { # <spec-file>
  local number
  number=$(basename "$1" | cut -c1-4)
  find "$TAFFY_ROOT/$TAFFY_FORK_QUEUE" -type f -name "$number-*.patch" 2>/dev/null \
    | grep -q .
}

# The value of a specification's `**Status:**` field: everything after the
# label, including the continuation lines, and stopping at a blank line or at
# the next `**Field:**` the header starts. Read whole rather than by first line,
# because the queue wraps its prose at the same width as the rest of the
# repository and a verdict routinely runs past the first line.
taffy_fork_spec_status() { # <spec-file>
  awk '
    /^\*\*Status:\*\*/ {
      inside = 1
      sub(/^\*\*Status:\*\*[[:space:]]*/, "")
      print
      next
    }
    inside && /^[[:space:]]*$/ { exit }
    inside && /^\*\*[A-Za-z][^*]*:\*\*/ { exit }
    inside { print }
  ' "$1"
}

# 0 when this specification has been retired: the change it describes is one the
# fork has decided not to make, so it is not queued work and must not be
# projected as though it were.
#
# The marker is a **bold** run carrying the word and *opening* the status, which
# is how both retirements in the queue are written. Both halves of that rule are
# load-bearing. Requiring the word alone would take
# 0006-bind-page-intelligence-service.md with it, whose status is `[Current]`
# and whose prose says mid-sentence that a superseded argument is **retired**.
# Requiring bold alone would take 0008-route-downloads-and-external-intents.md,
# whose `[Current]` status names an upstream commit but also says its generated
# queue file is still **owed**. Committed-but-unexported work is still owed by
# this repository, and still costs the fork its lines.
taffy_fork_spec_retired() { # <spec-file>
  taffy_fork_spec_status "$1" | tr '\n' ' ' \
    | grep -qiE '^\*\*[^*]*retired[^*]*\*\*'
}

# The estimate a specification records for itself, or empty when it records none
# in the queue's own form. Parsed rather than duplicated: chromium/patches/ is
# the authority for what each change costs, and a gate that kept its own copy
# would report a total no specification agrees with.
taffy_fork_spec_estimate() { # <spec-file>
  sed -n \
    's/^\*\*Estimated size:\*\*[^0-9]*\([0-9][0-9]*\)[[:space:]]*modified upstream lines.*/\1/p' \
    "$1" | head -n 1
}

# --- judging ----------------------------------------------------------------

# One rule for both figures against both bounds: over the bound fails, inside
# the warning band warns, anything else passes. The share of the budget is
# derived here, so no document has to keep a percentage in sync by hand.
taffy_fork_verdict() { # <label> <value> <bound> <unit> <remedy-line>...
  local label=$1 value=$2 bound=$3 unit=$4; shift 4
  local share floor shown line
  # Rounded to the nearest percent. chromium/patches/README.md deliberately
  # states no total and points here instead, so this is the only place either
  # figure or its share of the budget is ever written down.
  share=$(( (value * 200 + bound) / (bound * 2) ))
  floor=$(taffy_fork_warn_floor "$bound")
  shown="$(taffy_fork_commas "$value") $unit — ${share}% of the budget of $(taffy_fork_commas "$bound")"
  if [ "$value" -gt "$bound" ]; then
    hard "$label: $shown"
  elif [ "$value" -gt "$floor" ]; then
    soft "$label: $shown"
  else
    pass "$label: $shown"
    return 0
  fi
  for line in "$@"; do note "$line"; done
  return 0
}

# --- the gate ---------------------------------------------------------------

check_fork_debt() {
  step "chromium: fork-debt budget, realised and projected"

  if [ ! -d "$TAFFY_ROOT/$TAFFY_FORK_QUEUE" ]; then
    hard "missing patch queue directory: $TAFFY_FORK_QUEUE"
    note "It holds the only downstream edits to files Chromium owns. New TaffyGo"
    note "code belongs in taffy-core/ instead."
    return 0
  fi

  local realised_patches realised_lines
  realised_patches=$(taffy_fork_patch_files | grep -c . || true)
  realised_lines=$(taffy_fork_patched_lines)

  local spec pending_specs=0 pending_lines=0 estimate unreadable=''
  local retired_specs=0 retired_applied=''
  while IFS= read -r spec; do
    [ -n "$spec" ] || continue
    if taffy_fork_spec_retired "$spec"; then
      # The other direction, and the one that matters more: a retirement is a
      # statement that the tree no longer carries this edit. A .patch beside a
      # retired specification says the opposite, and one of the two is wrong.
      taffy_fork_spec_exported "$spec" &&
        retired_applied="$retired_applied ${spec#"$TAFFY_ROOT"/}"
      retired_specs=$((retired_specs + 1))
      continue
    fi
    taffy_fork_spec_exported "$spec" && continue
    estimate=$(taffy_fork_spec_estimate "$spec")
    if [ -z "$estimate" ]; then
      unreadable="$unreadable ${spec#"$TAFFY_ROOT"/}"
      continue
    fi
    pending_specs=$((pending_specs + 1))
    pending_lines=$((pending_lines + estimate))
  done < <(taffy_fork_spec_files)

  if [ -n "$retired_applied" ]; then
    hard "retired specification(s) whose patch is still exported:$retired_applied"
    note "A retired change is one the fork does not make, so an exported .patch"
    note "beside it is an edit the build still applies. Drop the commit from"
    note "taffy/patched and re-run ./tools/chromium/export-patches, or correct the"
    note "status line — the queue cannot mean both."
  fi

  if [ -n "$unreadable" ]; then
    hard "specification(s) with no readable estimate:$unreadable"
    note "The projection sums what each specification records about itself, so a"
    note "missing estimate silently understates it. Add the header line to each:"
    note "  **Estimated size:** ~N modified upstream lines, M files"
  fi

  # Two remediations, because the two figures are fixed at different moments.
  local realised_remedy_1 realised_remedy_2 projected_remedy_1 projected_remedy_2
  realised_remedy_1="Refactor the delta into //taffy or upstream it; a bound that is"
  realised_remedy_2="exceeded blocks new upstream-file edits until the queue is back inside it."
  projected_remedy_1="Retire or refactor a specification in $TAFFY_FORK_QUEUE/ before it is written."
  projected_remedy_2="Moving an edit into //taffy costs an argument now and a rebase every milestone later."

  # Realised: what a build would apply today.
  if [ "$realised_patches" -eq 0 ]; then
    pass "realised: the patch queue is empty — 0 patches, 0 modified upstream lines"
    if [ "$pending_specs" -gt 0 ]; then
      note "Empty because no specification has been exported, not because the fork"
      note "delta was measured and found small. Next step: write the edits in the"
      note "checkout and run ./tools/chromium/export-patches."
    fi
  else
    taffy_fork_verdict "realised" "$realised_patches" "$TAFFY_FORK_MAX_PATCHES" \
      "patches" "$realised_remedy_1" "$realised_remedy_2"
    taffy_fork_verdict "realised" "$realised_lines" "$TAFFY_FORK_MAX_LINES" \
      "modified upstream lines" "$realised_remedy_1" "$realised_remedy_2"
  fi

  # Projected: what the queue holds once every specification becomes a patch.
  # Retirement is the one thing that takes a specification out of that sum, and
  # saying so is the point: the remedy below promises that retiring a change
  # sheds projected debt, and a figure that ignored the retirement would make
  # the promise a lie.
  if [ "$retired_specs" -gt 0 ]; then
    note "$retired_specs specification(s) are retired and are not projected — a retired"
    note "change is one the fork has decided not to make."
  fi

  if [ "$pending_specs" -eq 0 ]; then
    if [ "$realised_patches" -eq 0 ]; then
      pass "projected: nothing exported and nothing specified — no fork debt planned"
    else
      pass "projected: every specification is exported or retired — the realised figures are the whole debt"
    fi
    return 0
  fi

  note "projected = realised + $pending_specs specification(s) at their own recorded estimates"
  taffy_fork_verdict "projected" "$((realised_patches + pending_specs))" \
    "$TAFFY_FORK_MAX_PATCHES" "patches" "$projected_remedy_1" "$projected_remedy_2"
  taffy_fork_verdict "projected" "$((realised_lines + pending_lines))" \
    "$TAFFY_FORK_MAX_LINES" "modified upstream lines" "$projected_remedy_1" "$projected_remedy_2"
  note "An estimate is an estimate, and a specification may retire before it is"
  note "written; only the realised figures gate a build."
  return 0
}
