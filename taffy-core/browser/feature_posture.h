// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FEATURE_POSTURE_H_
#define TAFFY_BROWSER_FEATURE_POSTURE_H_

#include "base/containers/span.h"
// base::Feature cannot be forward-declared: its real declaration carries an
// abi_tag attribute, and a plain redeclaration is a compile error.
#include "base/feature_list.h"
#include "base/memory/raw_ptr_exclusion.h"

class PrefRegistrySimple;

// The runtime half of the Google service posture (decision 0019,
// docs/decisions/0019-google-service-posture.md).
//
// Decision 0019 assigns every Google-bound subsystem one of five postures.
// The ones marked "disabled (runtime)" are turned off here, in exactly two
// ways, because the build-time postures live in the committed GN args and
// nowhere else:
//
//   * a base::Feature override, registered before the feature list freezes —
//     installed by patch 0015 from
//     ChromeBrowserFieldTrials::RegisterFeatureOverrides;
//   * a profile pref *default* — installed by patch 0016 at the end of
//     RegisterProfilePrefs. A default rather than a forced value, so the
//     user can still turn the feature on, the pref honestly reads as
//     user-set when they do, and enterprise policy behaves unchanged.
//
// Both lists are data, walked by the product and by the unit test alike, so
// an entry cannot be applied without being tested or tested without being
// applied. Every entry names the upstream symbol it silences and the reason;
// an upstream rename is a compile error here, which is the failure mode
// worth having — the build stops instead of quietly reviving a service.

namespace taffy {

// One feature decision 0019 overrides.
struct FeaturePostureEntry {
  // Never null. A pointer rather than a reference so the table is an
  // aggregate the compiler lays out at build time.
  // RAW_PTR_EXCLUSION: every entry is a static-storage-duration constant
  // pointing at a base::Feature global; nothing here can dangle, and raw_ptr
  // would trade per-access overhead for protection those pointers cannot
  // need.
  RAW_PTR_EXCLUSION const base::Feature* feature;

  // The state the posture forces. Almost every entry disables; the rare
  // enable exists for upstream flags whose *on* state is the quiet one
  // (their name says "avoid" or "suppress"). The test walks this field, so
  // an entry cannot claim one direction and apply the other.
  base::FeatureList::OverrideState state;

  // Why, in one sentence a reviewer can act on.
  const char* reason;
};

// One profile pref whose default decision 0019 changes.
struct PrefPostureEntry {
  const char* pref_name;
  bool default_value;
  const char* reason;
};

// Registers a disabling override for every feature in the posture list.
// Must run before base::FeatureList::SetInstance freezes the list; the seam
// that guarantees that is patch 0015's call site. First-wins semantics mean
// the compiled-in field-trial testing config would beat this list, which is
// why the committed GN profiles carry disable_fieldtrial_testing_config —
// the `chromium` lane gates that pairing.
void ApplyFeaturePosture(base::FeatureList* feature_list);

// Overrides the default of every pref in the posture list. Call after the
// prefs are registered, from the end of RegisterProfilePrefs (patch 0016).
void OverrideProfilePrefDefaults(PrefRegistrySimple* registry);

// The lists themselves, so tests walk exactly what the product applies.
base::span<const FeaturePostureEntry> FeaturePostureForTesting();
base::span<const PrefPostureEntry> PrefPostureForTesting();

}  // namespace taffy

#endif  // TAFFY_BROWSER_FEATURE_POSTURE_H_
