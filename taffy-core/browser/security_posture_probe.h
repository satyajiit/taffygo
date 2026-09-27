// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SECURITY_POSTURE_PROBE_H_
#define TAFFY_BROWSER_SECURITY_POSTURE_PROBE_H_

#include "taffy/browser/security_posture.h"

// Gathers the facts of PAR-SEC-001 and PAR-SEC-008 from the running browser.
//
// This is the one file in the seam that talks to Chromium, and it is
// deliberately the smallest one: it reads the command line, asks
// content::SiteIsolationPolicy what it is enforcing, and asks the embedder for
// the two Safe Browsing facts that only the embedder knows. It contains no
// judgement — that is SecurityPostureEvaluator, which is pure and therefore
// testable — and it changes nothing it observes.
//
// The embedder delegate exists for a layering reason rather than a testing
// one. Whether Safe Browsing is on for a fresh profile is a preference in
// //chrome, and //taffy must not reach upward. The browser layer
// implements this interface; with no implementation registered, both facts
// report false, which is the fail-closed answer and produces a finding rather
// than silence.

namespace taffy {

class SecurityPostureEmbedderSource {
 public:
  virtual ~SecurityPostureEmbedderSource() = default;

  // Whether Safe Browsing is on for a newly created profile with no user
  // changes. Not "for the current profile": a user who turned it off is a
  // decision the product respects, and a posture report that flagged it would
  // be reporting the user rather than the build.
  virtual bool IsSafeBrowsingEnabledByDefault() = 0;
};

class SecurityPostureProbe {
 public:
  // Null clears it. With no source the Safe Browsing default reports false.
  static void SetEmbedderSource(SecurityPostureEmbedderSource* source);

  // Reads the current process. Safe to call from the UI thread at any point
  // after the command line is initialised.
  static SecurityPosture Gather();

 private:
  SecurityPostureProbe() = delete;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_SECURITY_POSTURE_PROBE_H_
