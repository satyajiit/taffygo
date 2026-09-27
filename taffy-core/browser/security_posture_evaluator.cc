// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_evaluator.h"

namespace taffy {

namespace {

// The findings that make a build unfit to hand to a person, as opposed to a
// configuration question somebody can record a decision about. The sandbox,
// site isolation, TLS and web security are properties of the engine; the Safe
// Browsing default is a setting.
constexpr uint32_t kDisqualifyingMask =
    AsMask(SecurityPostureViolation::kRendererSandboxDisabled) |
    AsMask(SecurityPostureViolation::kGpuSandboxDisabled) |
    AsMask(SecurityPostureViolation::kSiteIsolationDisabled) |
    AsMask(SecurityPostureViolation::kSiteIsolationUnknown) |
    AsMask(SecurityPostureViolation::kTlsVerificationRelaxed) |
    AsMask(SecurityPostureViolation::kWebSecurityDisabled) |
    AsMask(SecurityPostureViolation::kSafeBrowsingAbsentFromBuild) |
    AsMask(SecurityPostureViolation::kDangerousSwitchPresent);

}  // namespace

SecurityPostureVerdict EvaluateSecurityPosture(
    const SecurityPosture& posture) {
  uint32_t violations = AsMask(SecurityPostureViolation::kNone);

  if (!posture.renderer_sandbox_enabled) {
    violations |= AsMask(SecurityPostureViolation::kRendererSandboxDisabled);
  }
  if (!posture.gpu_sandbox_enabled) {
    violations |= AsMask(SecurityPostureViolation::kGpuSandboxDisabled);
  }

  switch (posture.site_isolation) {
    case SiteIsolationMode::kUnknown:
      // "We could not tell" and "it is off" have the same consequence for a
      // reviewer, so they are both findings — with different names, because
      // the fix is different.
      violations |= AsMask(SecurityPostureViolation::kSiteIsolationUnknown);
      break;
    case SiteIsolationMode::kDisabled:
      violations |= AsMask(SecurityPostureViolation::kSiteIsolationDisabled);
      break;
    case SiteIsolationMode::kIsolatedOriginsOnly:
      // Upstream's own memory-constrained Android configuration. Not a
      // finding: reporting one here would mean reporting a violation on every
      // phone the product targets, and a check that always fires is a check
      // nobody reads.
      break;
    case SiteIsolationMode::kStrictSitePerProcess:
      break;
  }

  if (!posture.tls_certificate_verification_enforced) {
    violations |= AsMask(SecurityPostureViolation::kTlsVerificationRelaxed);
  }
  if (!posture.web_security_enabled) {
    violations |= AsMask(SecurityPostureViolation::kWebSecurityDisabled);
  }
  if (!posture.safe_browsing_available_in_build) {
    violations |= AsMask(SecurityPostureViolation::kSafeBrowsingAbsentFromBuild);
  } else if (!posture.safe_browsing_enabled_by_default) {
    violations |= AsMask(SecurityPostureViolation::kSafeBrowsingOffByDefault);
  }
  if (posture.dangerous_switch_mask != AsMask(DangerousSwitch::kNone)) {
    violations |= AsMask(SecurityPostureViolation::kDangerousSwitchPresent);
  }
  if (!posture.metrics_upload_endpoint_absent) {
    // A privacy-posture finding (decision 0019), not a security one, so it is
    // deliberately outside the disqualifying mask: the fix is a decision
    // record, not withdrawing the build from people.
    violations |= AsMask(SecurityPostureViolation::kMetricsUploadEndpointPresent);
  }

  SecurityPostureVerdict verdict;
  verdict.violation_mask = violations;
  verdict.is_compliant = violations == AsMask(SecurityPostureViolation::kNone);
  verdict.has_disqualifying_finding = (violations & kDisqualifyingMask) != 0;
  return verdict;
}

const char* DescribeSecurityPostureViolation(SecurityPostureViolation value) {
  switch (value) {
    case SecurityPostureViolation::kNone:
      return "";
    case SecurityPostureViolation::kRendererSandboxDisabled:
      return "The renderer sandbox is not enabled.";
    case SecurityPostureViolation::kGpuSandboxDisabled:
      return "The GPU process sandbox is not enabled.";
    case SecurityPostureViolation::kSiteIsolationDisabled:
      return "Site isolation is disabled.";
    case SecurityPostureViolation::kSiteIsolationUnknown:
      return "The site isolation mode could not be established.";
    case SecurityPostureViolation::kTlsVerificationRelaxed:
      return "Certificate verification is not being enforced.";
    case SecurityPostureViolation::kWebSecurityDisabled:
      return "Web security is disabled.";
    case SecurityPostureViolation::kSafeBrowsingAbsentFromBuild:
      return "Safe Browsing is not compiled into this build.";
    case SecurityPostureViolation::kSafeBrowsingOffByDefault:
      return "Safe Browsing is compiled in but off by default.";
    case SecurityPostureViolation::kDangerousSwitchPresent:
      return "A command-line switch that disables a protection is present.";
    case SecurityPostureViolation::kMetricsUploadEndpointPresent:
      return "A metrics upload endpoint is present in this build; decision "
             "0019 records the upload path as inert.";
  }
}

}  // namespace taffy
