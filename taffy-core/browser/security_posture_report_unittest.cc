// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_report.h"

#include <string>
#include <vector>

#include "base/check.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-SEC-002: the build reports the Chromium version, the patch delta and the
// known lag.
//
// The report reads the generated provenance, so this file asserts shape and
// arithmetic rather than particular values — the values move whenever the pin
// moves, and a test that pinned them would fail every rebase for no reason.
// The one thing asserted exactly is the rule that lag is computed at display
// time: DaysSinceUpstreamPin takes "now" as an argument, and these tests pass
// several different values of it.

namespace taffy {
namespace {

SecurityPosture CompliantPosture() {
  SecurityPosture posture;
  posture.renderer_sandbox_enabled = true;
  posture.gpu_sandbox_enabled = true;
  posture.site_isolation = SiteIsolationMode::kStrictSitePerProcess;
  posture.tls_certificate_verification_enforced = true;
  posture.web_security_enabled = true;
  posture.safe_browsing_available_in_build = true;
  posture.safe_browsing_enabled_by_default = true;
  posture.metrics_upload_endpoint_absent = true;
  return posture;
}

base::Time AtUtc(const char* iso_date) {
  base::Time parsed;
  CHECK(base::Time::FromUTCString(iso_date, &parsed));
  return parsed;
}

TEST(SecurityPostureReportTest, TheReportCarriesTheGeneratedProvenance) {
  const SecurityPostureReport report =
      BuildSecurityPostureReport(CompliantPosture());

  // Which values these are is chromium/REVISION's business; that they are
  // present is this report's.
  EXPECT_FALSE(report.chromium_milestone.empty());
  EXPECT_FALSE(report.chromium_tag.empty());
  EXPECT_FALSE(report.chromium_commit.empty());
  EXPECT_GE(report.downstream_patch_count, 0);
  EXPECT_GE(report.downstream_modified_upstream_lines, 0);
  EXPECT_GE(report.security_patch_level, 0);
  EXPECT_FALSE(report.security_advisories.empty());
}

TEST(SecurityPostureReportTest, ThePostureAndItsVerdictTravelTogether) {
  SecurityPosture posture = CompliantPosture();
  posture.renderer_sandbox_enabled = false;

  const SecurityPostureReport report = BuildSecurityPostureReport(posture);
  EXPECT_FALSE(report.verdict.is_compliant);
  EXPECT_TRUE(report.verdict.has_disqualifying_finding);
  EXPECT_FALSE(report.posture.renderer_sandbox_enabled);
}

TEST(SecurityPostureReportTest, LagIsComputedFromTheSuppliedTime) {
  SecurityPostureReport report = BuildSecurityPostureReport(CompliantPosture());
  report.pin_recorded_date = "2026-08-16";
  report.pin_date_is_recorded = true;

  EXPECT_EQ(0, DaysSinceUpstreamPin(report, AtUtc("2026-08-16")));
  EXPECT_EQ(1, DaysSinceUpstreamPin(report, AtUtc("2026-08-17")));
  EXPECT_EQ(30, DaysSinceUpstreamPin(report, AtUtc("2026-09-15")));

  // The same report, two different answers, from two different "now" values.
  // That is the property: nothing about the lag is baked into the binary, so
  // an identical source tree still produces an identical binary.
  EXPECT_NE(DaysSinceUpstreamPin(report, AtUtc("2026-08-17")),
            DaysSinceUpstreamPin(report, AtUtc("2026-09-15")));
}

TEST(SecurityPostureReportTest, AnUnrecordedPinDateReportsNoLag) {
  SecurityPostureReport report = BuildSecurityPostureReport(CompliantPosture());
  report.pin_recorded_date.clear();
  report.pin_date_is_recorded = false;

  EXPECT_EQ(-1, DaysSinceUpstreamPin(report, AtUtc("2026-09-15")));
}

TEST(SecurityPostureReportTest, AClockBehindThePinReportsNoLag) {
  // A device whose clock is wrong must not be told it is ahead of upstream,
  // which is not a thing.
  SecurityPostureReport report = BuildSecurityPostureReport(CompliantPosture());
  report.pin_recorded_date = "2026-08-16";
  report.pin_date_is_recorded = true;

  EXPECT_EQ(-1, DaysSinceUpstreamPin(report, AtUtc("2026-01-01")));
}

TEST(SecurityPostureReportTest, TheFormattedRowsCoverBothParityRows) {
  SecurityPostureReport report = BuildSecurityPostureReport(CompliantPosture());
  report.pin_recorded_date = "2026-08-16";
  report.pin_date_is_recorded = true;

  const std::vector<SecurityPostureReportRow> rows =
      FormatSecurityPostureReport(report, AtUtc("2026-08-26"));

  auto find = [&rows](const std::string& label) -> std::string {
    for (const SecurityPostureReportRow& row : rows) {
      if (row.label == label) {
        return row.value;
      }
    }
    return std::string();
  };

  // PAR-SEC-002: version, patch delta, known lag.
  EXPECT_FALSE(find("Chromium version").empty());
  EXPECT_FALSE(find("Downstream changes").empty());
  EXPECT_EQ("10", find("Days behind the pinned revision"));

  // PAR-SEC-001 and PAR-SEC-008: nothing was switched off.
  EXPECT_EQ("on", find("Renderer sandbox"));
  EXPECT_EQ("a process per site", find("Site isolation"));
  EXPECT_EQ("enforced", find("Certificate verification"));
  EXPECT_EQ("on", find("Web security"));
  EXPECT_EQ("on by default", find("Safe Browsing"));
  EXPECT_EQ("none", find("Findings"));
}

TEST(SecurityPostureReportTest, FindingsAreReportedWorstFirst) {
  SecurityPosture posture = CompliantPosture();
  posture.renderer_sandbox_enabled = false;
  posture.safe_browsing_enabled_by_default = false;
  posture.dangerous_switch_mask = AsMask(DangerousSwitch::kNoSandbox);

  const SecurityPostureReport report = BuildSecurityPostureReport(posture);
  const std::vector<SecurityPostureReportRow> rows =
      FormatSecurityPostureReport(report, AtUtc("2026-08-26"));

  std::vector<std::string> findings;
  for (const SecurityPostureReportRow& row : rows) {
    if (row.label == "Finding") {
      findings.push_back(row.value);
    }
  }

  ASSERT_GE(findings.size(), 3u);
  // The first line a reviewer reads is the worst one.
  EXPECT_EQ("The renderer sandbox is not enabled.", findings.front());
}

TEST(SecurityPostureReportTest, AnInstrumentedBuildSaysSoInTheReport) {
  SecurityPosture posture = CompliantPosture();
  posture.is_instrumented_build = true;

  const std::vector<SecurityPostureReportRow> rows =
      FormatSecurityPostureReport(BuildSecurityPostureReport(posture),
                                  AtUtc("2026-08-26"));

  bool found = false;
  for (const SecurityPostureReportRow& row : rows) {
    if (row.label == "Build instrumentation") {
      found = true;
    }
  }
  // A posture gathered from a sanitizer build must never be published as
  // evidence for a shipping one, so it is in the report rather than in a
  // footnote somebody drops.
  EXPECT_TRUE(found);
}

TEST(SecurityPostureReportTest, AnUnrecordedPinDateIsSaidPlainly) {
  SecurityPostureReport report = BuildSecurityPostureReport(CompliantPosture());
  report.pin_recorded_date.clear();
  report.pin_date_is_recorded = false;

  const std::vector<SecurityPostureReportRow> rows =
      FormatSecurityPostureReport(report, AtUtc("2026-08-26"));

  for (const SecurityPostureReportRow& row : rows) {
    if (row.label == "Revision pinned on" ||
        row.label == "Days behind the pinned revision") {
      EXPECT_EQ("not recorded", row.value);
    }
  }
}

}  // namespace
}  // namespace taffy
