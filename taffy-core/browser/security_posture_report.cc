// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_report.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "taffy/browser/security_posture_evaluator.h"

namespace taffy {

namespace {

// The findings, in report order. Ordered by severity rather than by bit value
// so that the first line a reviewer reads is the worst one.
constexpr SecurityPostureViolation kViolationsInReportOrder[] = {
    SecurityPostureViolation::kRendererSandboxDisabled,
    SecurityPostureViolation::kSiteIsolationDisabled,
    SecurityPostureViolation::kWebSecurityDisabled,
    SecurityPostureViolation::kTlsVerificationRelaxed,
    SecurityPostureViolation::kGpuSandboxDisabled,
    SecurityPostureViolation::kSafeBrowsingAbsentFromBuild,
    SecurityPostureViolation::kSiteIsolationUnknown,
    SecurityPostureViolation::kDangerousSwitchPresent,
    SecurityPostureViolation::kSafeBrowsingOffByDefault,
    SecurityPostureViolation::kMetricsUploadEndpointPresent,
};

const char* DescribeSiteIsolation(SiteIsolationMode mode) {
  switch (mode) {
    case SiteIsolationMode::kUnknown:
      return "not established";
    case SiteIsolationMode::kDisabled:
      return "disabled";
    case SiteIsolationMode::kIsolatedOriginsOnly:
      return "isolated origins";
    case SiteIsolationMode::kStrictSitePerProcess:
      return "a process per site";
  }
}

}  // namespace

SecurityPostureReport BuildSecurityPostureReport(
    const SecurityPosture& posture) {
  const UpstreamProvenance& provenance = GetUpstreamProvenance();

  SecurityPostureReport report;
  report.chromium_milestone = std::string(provenance.chromium_milestone);
  report.chromium_tag = std::string(provenance.chromium_tag);
  report.chromium_commit = std::string(provenance.chromium_commit);
  report.pin_recorded_date = std::string(provenance.pin_recorded_date);
  report.pin_date_is_recorded = !report.pin_recorded_date.empty();
  report.downstream_patch_count = provenance.downstream_patch_count;
  report.downstream_modified_upstream_lines =
      provenance.downstream_modified_upstream_lines;
  report.security_patch_level = provenance.security_patch_level;
  report.security_advisories = std::string(provenance.security_advisories);

  report.posture = posture;
  report.verdict = EvaluateSecurityPosture(posture);
  return report;
}

int DaysSinceUpstreamPin(const SecurityPostureReport& report,
                         base::Time now) {
  if (!report.pin_date_is_recorded) {
    return -1;
  }

  base::Time pinned_at;
  // VERIFY AT SP-01: base::Time::FromUTCString at the pinned milestone.
  // Upstream file to read: base/time/time.h. The provenance date is ISO 8601
  // (YYYY-MM-DD), which that parser accepts. If it does not, the fallback is
  // base::Time::FromUTCExploded over three integers split here; the contract
  // that matters is that no clock is read at build time.
  if (!base::Time::FromUTCString(report.pin_recorded_date.c_str(),
                                 &pinned_at)) {
    return -1;
  }
  if (now < pinned_at) {
    // A device clock behind the pin date. Reporting a negative lag would be
    // worse than reporting none: a reviewer would read it as "ahead of
    // upstream", which is not a thing.
    return -1;
  }
  return static_cast<int>((now - pinned_at).InDays());
}

std::vector<SecurityPostureReportRow> FormatSecurityPostureReport(
    const SecurityPostureReport& report,
    base::Time now) {
  std::vector<SecurityPostureReportRow> rows;

  rows.push_back({"Chromium milestone", report.chromium_milestone});
  rows.push_back({"Chromium version", report.chromium_tag});
  rows.push_back({"Chromium revision", report.chromium_commit});
  rows.push_back({"Revision pinned on", report.pin_date_is_recorded
                                            ? report.pin_recorded_date
                                            : "not recorded"});

  const int lag = DaysSinceUpstreamPin(report, now);
  rows.push_back({"Days behind the pinned revision",
                  lag < 0 ? "not recorded" : base::NumberToString(lag)});

  rows.push_back({"Downstream changes",
                  base::NumberToString(report.downstream_patch_count)});
  rows.push_back(
      {"Downstream changed lines",
       base::NumberToString(report.downstream_modified_upstream_lines)});
  rows.push_back({"Security fixes applied on top",
                  base::NumberToString(report.security_patch_level)});
  rows.push_back({"Security references", report.security_advisories});

  rows.push_back({"Renderer sandbox",
                  report.posture.renderer_sandbox_enabled ? "on" : "off"});
  rows.push_back(
      {"Site isolation", DescribeSiteIsolation(report.posture.site_isolation)});
  rows.push_back({"Certificate verification",
                  report.posture.tls_certificate_verification_enforced
                      ? "enforced"
                      : "relaxed"});
  rows.push_back(
      {"Web security", report.posture.web_security_enabled ? "on" : "off"});
  rows.push_back({"Safe Browsing",
                  !report.posture.safe_browsing_available_in_build
                      ? "not in this build"
                      : (report.posture.safe_browsing_enabled_by_default
                             ? "on by default"
                             : "off by default")});

  if (report.posture.is_instrumented_build) {
    // A posture gathered from a sanitizer or fuzzer build must never be
    // published as evidence for a shipping one, so the report says so in the
    // report rather than in a footnote somebody drops.
    rows.push_back({"Build instrumentation",
                    "this build is instrumented and is not a release build"});
  }

  for (SecurityPostureViolation violation : kViolationsInReportOrder) {
    if (HasViolation(report.verdict.violation_mask, violation)) {
      rows.push_back(
          {"Finding", DescribeSecurityPostureViolation(violation)});
    }
  }
  if (report.verdict.is_compliant) {
    rows.push_back({"Findings", "none"});
  }

  return rows;
}

}  // namespace taffy
