// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SECURITY_POSTURE_ASSERTIONS_H_
#define TAFFY_BROWSER_SECURITY_POSTURE_ASSERTIONS_H_

#include "build/branding_buildflags.h"
#include "build/build_config.h"
// VERIFY AT SP-01: the Safe Browsing build flag header and the spelling of its
// flags at the pinned milestone. Upstream files to read:
// components/safe_browsing/buildflags.gni and the generated
// components/safe_browsing/buildflags.h. The flags this file uses are
// SAFE_BROWSING_AVAILABLE (any Safe Browsing at all) and FULL_SAFE_BROWSING
// (the desktop feature set, which an Android build does not have). If the
// names differ, fix them here — there is one use of each, and the run-time
// half in security_posture_probe.cc reports the same fact independently, so a
// build-flag rename cannot silently remove the coverage.
#include "components/safe_browsing/buildflags.h"

// The compile-time half of PAR-SEC-001 and PAR-SEC-008.
//
// A run-time probe can only report what the binary it is running in was built
// to do. These assertions catch the other half: a build configuration that
// removed a protection before the browser ever started. They are in a header
// so that both the production code and the test binary include them, which
// means the check cannot be lost by a target that forgets to link a particular
// translation unit.
//
// Two rules for anything added here:
//
//   1. **It must be a fact about this build, not a preference.** An assertion
//      that fails for a legitimate developer configuration will be commented
//      out within a week, and a commented-out assertion is worse than none.
//   2. **It must name what to do.** A static assertion whose message is
//      "invariant violated" costs the reader an hour. Every message below says
//      which decision record or parity row governs the change.
//
// The build-time complement to this file lives in browser/BUILD.gn, which
// asserts the GN arguments that C++ cannot see.

namespace taffy {

// Android is the only product target today. The portable browser layer also
// compiles on Chromium's desktop families so macOS and Windows can become
// build canaries before either is claimed as a product. Product assembly stays
// Android-only in //taffy:product until OD-077 is resolved; allowing this
// component to compile is not a release or support claim.
static_assert(BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) ||
                  BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) ||
                  BUILDFLAG(IS_WIN),
              "//taffy browser code supports Chromium's Android and desktop "
              "build families. Product assembly and support claims remain "
              "separate decisions (0036 and OD-077).");

// Safe Browsing must be compiled in. PAR-SEC-001 forbids disabling it without
// an approved decision record and threat review, and PAR-SEC-007 preserves the
// upstream baseline from M1. A build with safe_browsing_mode = 0 cannot be
// configured to have it, so this is the earliest possible place to notice.
static_assert(BUILDFLAG(SAFE_BROWSING_AVAILABLE),
              "This build has no Safe Browsing. PAR-SEC-001 requires an "
              "approved decision record and threat review before a Chromium "
              "protection is removed; see chromium/args/*.gn for the GN "
              "argument and docs/open-decisions.md OD-053 for the provider "
              "strategy.");

// The build must not be Chrome-branded. Decision 0019's inventory marks the
// variations seed fetch, the metrics and crash uploads, and other Google
// services as "inert without keys or branding" — postures that hold because
// upstream gates those services on GOOGLE_CHROME_BRANDING. A branded build
// would revive them wholesale (and ship branding the derivative has no
// licence to), so the posture record's ground assumption is asserted where
// it cannot be lost.
static_assert(!BUILDFLAG(GOOGLE_CHROME_BRANDING),
              "This build is Chrome-branded. The Google service posture "
              "(docs/decisions/0019-google-service-posture.md) and the "
              "licensing boundary in chromium/README.md both assume an "
              "unbranded build; a branded one revives the variations, "
              "metrics and crash uploads the posture records as inert.");

// A sanitizer or fuzzer build relaxes checks that a shipping build relies on.
// It is a legitimate configuration and it is never a configuration whose
// security posture may be published as evidence, so the posture records it
// rather than the build refusing it. This constant is what the run-time probe
// reports; keeping the definition here means the compile-time and run-time
// halves cannot disagree.
#if defined(ADDRESS_SANITIZER) || defined(THREAD_SANITIZER) ||                 \
    defined(MEMORY_SANITIZER) || defined(LEAK_SANITIZER) ||                    \
    defined(UNDEFINED_SANITIZER) || defined(FUZZING_BUILD_MODE)
inline constexpr bool kIsInstrumentedBuild = true;
#else
inline constexpr bool kIsInstrumentedBuild = false;
#endif

// A tripwire, not a mechanism. Nothing in TaffyGo defines these; they exist so
// that the shortest path to switching a protection off — adding a define —
// stops at a message naming the review it needs. Anyone who genuinely has to
// do it deletes the assertion in the same change, which is exactly the change
// a reviewer should be looking at.
#if defined(TAFFY_DISABLE_RENDERER_SANDBOX)
static_assert(false,
              "TAFFY_DISABLE_RENDERER_SANDBOX is defined. Disabling the "
              "renderer sandbox needs an approved decision record and a threat "
              "review (PAR-SEC-001, PAR-SEC-008).");
#endif
#if defined(TAFFY_DISABLE_SITE_ISOLATION)
static_assert(false,
              "TAFFY_DISABLE_SITE_ISOLATION is defined. Site isolation is a "
              "PAR-SEC-008 requirement and architecture tests confirm it.");
#endif
#if defined(TAFFY_DISABLE_TLS_VERIFICATION)
static_assert(false,
              "TAFFY_DISABLE_TLS_VERIFICATION is defined. Certificate "
              "verification is not something a fork switches off (PAR-SEC-001, "
              "PAR-NAV-007).");
#endif
#if defined(TAFFY_DISABLE_SAFE_BROWSING)
static_assert(false,
              "TAFFY_DISABLE_SAFE_BROWSING is defined. See PAR-SEC-001 and the "
              "reputation-service strategy in docs/open-decisions.md OD-053.");
#endif

} // namespace taffy

#endif // TAFFY_BROWSER_SECURITY_POSTURE_ASSERTIONS_H_
