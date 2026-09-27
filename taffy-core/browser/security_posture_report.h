// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SECURITY_POSTURE_REPORT_H_
#define TAFFY_BROWSER_SECURITY_POSTURE_REPORT_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "taffy/resources/branding/taffy_upstream_provenance.h"
#include "taffy/browser/security_posture.h"

// The single answer to "what is this build, and what did it switch off"
// (PAR-SEC-002: the build reports the Chromium version, the patch delta and
// the known lag; PAR-SEC-001 and PAR-SEC-008: nothing was disabled).
//
// The two halves already exist and neither is restated here.
// //taffy/resources/branding owns the provenance — the milestone, the tag,
// the commit, the patch count, the changed-line count and the security patch
// level, all generated from chromium/REVISION, chromium/SECURITY_PATCH_LEVEL
// and chromium/patches/. SecurityPostureProbe owns the run-time facts. This
// file joins them, because the two questions a security reviewer asks are
// always asked together and answering them from two places is how they end up
// describing two different builds.
//
// **Lag is computed here, at display time, and never at build time.** The
// provenance carries the date the revision was pinned; the caller supplies
// "now". A build that embedded "days behind" would embed the build clock, and
// an identical source tree would stop producing an identical binary. That
// constraint is the reason DaysSinceUpstreamPin takes an argument instead of
// reading a clock.

namespace taffy {

struct SecurityPostureReport {
  // Copied, not referenced: the provenance is a set of string views into
  // compile-time constants, and a report that outlived them would be a report
  // of dangling views. Copying into owned strings is what makes this struct
  // safe to hand to a surface that formats it later.
  std::string chromium_milestone;
  std::string chromium_tag;
  std::string chromium_commit;
  std::string pin_recorded_date;
  int downstream_patch_count = 0;
  int downstream_modified_upstream_lines = 0;
  int security_patch_level = 0;
  std::string security_advisories;

  SecurityPosture posture;
  SecurityPostureVerdict verdict;

  // True when the provenance generator ran outside a Git checkout and could
  // not record the pin date. A surface must say exactly that rather than
  // showing a lag it cannot support.
  bool pin_date_is_recorded = false;
};

// Joins the generated provenance with a gathered posture and judges the
// result. Pure with respect to its argument: the provenance is a compile-time
// constant of the binary.
SecurityPostureReport BuildSecurityPostureReport(const SecurityPosture& posture);

// How far behind the pinned upstream revision this build is, in whole days, or
// -1 when the pin date was not recorded. `now` is supplied by the caller for
// the reason in the file comment.
int DaysSinceUpstreamPin(const SecurityPostureReport& report, base::Time now);

// The report as ordered label and value pairs, for a surface that renders rows
// — the version page, the Android about screen, and the parity evidence
// artifact. Content-free: every value is a build constant or a categorical
// finding, and none of them is derived from browsing.
struct SecurityPostureReportRow {
  std::string label;
  std::string value;
};
std::vector<SecurityPostureReportRow> FormatSecurityPostureReport(
    const SecurityPostureReport& report,
    base::Time now);

}  // namespace taffy

#endif  // TAFFY_BROWSER_SECURITY_POSTURE_REPORT_H_
