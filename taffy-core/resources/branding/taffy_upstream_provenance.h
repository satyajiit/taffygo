// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BRANDING_TAFFY_UPSTREAM_PROVENANCE_H_
#define TAFFY_BRANDING_TAFFY_UPSTREAM_PROVENANCE_H_

#include <string_view>

// What upstream revision this binary is, and how far it has been changed.
//
// PAR-SEC-002 in the browser parity matrix is an M1 **Required** row: the
// build reports the Chromium version, the downstream patch delta, and the
// known lag. This header is that report's in-process half. The other half is
// the JSON sidecar the same generator writes for the release artifact
// manifest, so the number a person reads in the product and the number a
// release engineer reads in the manifest come from one generator run over one
// set of inputs.
//
// The values are produced by
// //taffy/resources/branding/tools/write_upstream_provenance.py from
// chromium/REVISION, chromium/SECURITY_PATCH_LEVEL and chromium/patches/ —
// the three inputs decision 0012 says the checkout is reproducible from. This
// header declares the shape; the generated .cc holds the values. Nothing here
// is ever hand-edited, and no other file in the fork restates a pin: the
// numbers live in exactly one place and this is a view of that place.
//
// Deliberately absent: the product version and the product name. Chromium's
// own version_info owns the first and //taffy/common/public owns the
// second. Lag in days is absent too, and that is a decision rather than an
// omission — a build that computed "days behind" at compile time would embed
// the build clock, and an identical source tree would stop producing an
// identical binary. This struct reports the date the pin was recorded and the
// surface that displays it does the subtraction.

namespace taffy {

struct UpstreamProvenance {
  // From chromium/REVISION. `chromium_commit` is the immutable upstream
  // commit of `chromium_tag`, never a branch.
  std::string_view chromium_milestone;
  std::string_view chromium_tag;
  std::string_view chromium_commit;

  // ISO 8601 date the pin was last changed, from the repository's own history.
  // Empty when the generator ran outside a Git checkout — for example from an
  // exported source archive. Empty means "not recorded", and a surface must
  // say exactly that rather than showing a lag it cannot support.
  std::string_view pin_recorded_date;

  // The fork-debt figures the strategy document budgets, counted the same way
  // ./tools/check fast counts them so the two can never disagree.
  int downstream_patch_count;
  int downstream_modified_upstream_lines;

  // From chromium/SECURITY_PATCH_LEVEL. The level resets to 0 at every
  // milestone rebase because the cherry-picks fold into the new revision;
  // `security_advisories` is the upstream references for a non-zero level and
  // is the literal string "none" at level 0.
  int security_patch_level;
  std::string_view security_advisories;
};

// Never partially filled, safe to call from any thread, and constant for the
// life of the process: every field is a compile-time constant of this binary.
const UpstreamProvenance& GetUpstreamProvenance();

}  // namespace taffy

#endif  // TAFFY_BRANDING_TAFFY_UPSTREAM_PROVENANCE_H_
