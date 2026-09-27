// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_evaluator.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// PAR-SEC-001 and PAR-SEC-008 as a table. The evaluator is pure, so the whole
// compliance matrix runs on any host — which is the point: a table that could
// only be exercised on a device would be a table nobody exercised.

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
  posture.dangerous_switch_mask = AsMask(DangerousSwitch::kNone);
  return posture;
}

TEST(SecurityPostureEvaluatorTest,
     AMetricsUploadEndpointIsAFindingButNotDisqualifying) {
  // Decision 0019 records the UMA/UKM uploads as inert because the public
  // tree's server URLs resolve empty. A build where one appears has revived
  // an upload path the posture record says does not exist — a finding whose
  // fix is a decision record, not withdrawing the build.
  SecurityPosture posture = CompliantPosture();
  posture.metrics_upload_endpoint_absent = false;

  const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
  EXPECT_FALSE(verdict.is_compliant);
  EXPECT_TRUE(
      HasViolation(verdict.violation_mask,
                   SecurityPostureViolation::kMetricsUploadEndpointPresent));
  EXPECT_FALSE(verdict.has_disqualifying_finding);
}

TEST(SecurityPostureEvaluatorTest, ACleanBuildHasNoFindings) {
  const SecurityPostureVerdict verdict =
      EvaluateSecurityPosture(CompliantPosture());

  EXPECT_TRUE(verdict.is_compliant);
  EXPECT_FALSE(verdict.has_disqualifying_finding);
  EXPECT_EQ(AsMask(SecurityPostureViolation::kNone), verdict.violation_mask);
}

TEST(SecurityPostureEvaluatorTest, ADisabledSandboxDisqualifiesTheBuild) {
  SecurityPosture posture = CompliantPosture();
  posture.renderer_sandbox_enabled = false;

  const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
  EXPECT_FALSE(verdict.is_compliant);
  EXPECT_TRUE(verdict.has_disqualifying_finding);
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kRendererSandboxDisabled));
}

TEST(SecurityPostureEvaluatorTest, IsolatedOriginsOnlyIsNotAFinding) {
  // Upstream's own memory-constrained Android configuration. Reporting a
  // violation here would mean reporting one on every phone the product
  // targets, and a check that always fires is a check nobody reads.
  SecurityPosture posture = CompliantPosture();
  posture.site_isolation = SiteIsolationMode::kIsolatedOriginsOnly;

  EXPECT_TRUE(EvaluateSecurityPosture(posture).is_compliant);
}

TEST(SecurityPostureEvaluatorTest, UnknownSiteIsolationIsItsOwnFinding) {
  // "We could not tell" and "it is off" have the same consequence and
  // different fixes, so they get different names.
  SecurityPosture posture = CompliantPosture();
  posture.site_isolation = SiteIsolationMode::kUnknown;

  const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kSiteIsolationUnknown));
  EXPECT_FALSE(HasViolation(verdict.violation_mask,
                            SecurityPostureViolation::kSiteIsolationDisabled));
  EXPECT_TRUE(verdict.has_disqualifying_finding);
}

TEST(SecurityPostureEvaluatorTest, RelaxedTlsVerificationIsDisqualifying) {
  SecurityPosture posture = CompliantPosture();
  posture.tls_certificate_verification_enforced = false;

  const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kTlsVerificationRelaxed));
  EXPECT_TRUE(verdict.has_disqualifying_finding);
}

TEST(SecurityPostureEvaluatorTest,
     SafeBrowsingAbsentAndSafeBrowsingOffAreDifferent) {
  SecurityPosture off = CompliantPosture();
  off.safe_browsing_enabled_by_default = false;
  const SecurityPostureVerdict off_verdict = EvaluateSecurityPosture(off);
  EXPECT_TRUE(HasViolation(off_verdict.violation_mask,
                           SecurityPostureViolation::kSafeBrowsingOffByDefault));
  // A setting somebody can record a decision about, not an unfit build.
  EXPECT_FALSE(off_verdict.has_disqualifying_finding);
  EXPECT_FALSE(off_verdict.is_compliant);

  SecurityPosture absent = CompliantPosture();
  absent.safe_browsing_available_in_build = false;
  const SecurityPostureVerdict absent_verdict =
      EvaluateSecurityPosture(absent);
  EXPECT_TRUE(
      HasViolation(absent_verdict.violation_mask,
                   SecurityPostureViolation::kSafeBrowsingAbsentFromBuild));
  // A build without it cannot be configured to have it.
  EXPECT_TRUE(absent_verdict.has_disqualifying_finding);
  // And it does not also report the default finding: there is no default to
  // report when the feature is not there.
  EXPECT_FALSE(
      HasViolation(absent_verdict.violation_mask,
                   SecurityPostureViolation::kSafeBrowsingOffByDefault));
}

TEST(SecurityPostureEvaluatorTest, AnyDangerousSwitchIsAFinding) {
  const std::vector<DangerousSwitch> switches = {
      DangerousSwitch::kNoSandbox,
      DangerousSwitch::kDisableWebSecurity,
      DangerousSwitch::kDisableSiteIsolation,
      DangerousSwitch::kIgnoreCertificateErrors,
      DangerousSwitch::kIgnoreCertificateErrorsSpkiList,
      DangerousSwitch::kAllowRunningInsecureContent,
      DangerousSwitch::kDisableGpuSandbox,
      DangerousSwitch::kTreatInsecureOriginAsSecure,
      DangerousSwitch::kDisableWebSecurityForTesting,
  };

  for (DangerousSwitch value : switches) {
    SecurityPosture posture = CompliantPosture();
    posture.dangerous_switch_mask = AsMask(value);
    const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
    EXPECT_TRUE(HasViolation(verdict.violation_mask,
                             SecurityPostureViolation::kDangerousSwitchPresent))
        << "switch bit " << AsMask(value);
    EXPECT_TRUE(verdict.has_disqualifying_finding);
  }
}

TEST(SecurityPostureEvaluatorTest, FindingsAccumulateRatherThanMasking) {
  SecurityPosture posture;  // Every field at its fail-closed default.

  const SecurityPostureVerdict verdict = EvaluateSecurityPosture(posture);
  EXPECT_FALSE(verdict.is_compliant);
  EXPECT_TRUE(verdict.has_disqualifying_finding);
  // A build in this state has several things wrong with it and a reviewer
  // needs to see all of them, not the first one.
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kRendererSandboxDisabled));
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kSiteIsolationUnknown));
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kTlsVerificationRelaxed));
  EXPECT_TRUE(HasViolation(verdict.violation_mask,
                           SecurityPostureViolation::kWebSecurityDisabled));
  EXPECT_TRUE(
      HasViolation(verdict.violation_mask,
                   SecurityPostureViolation::kSafeBrowsingAbsentFromBuild));
}

TEST(SecurityPostureEvaluatorTest, EveryViolationHasASentence) {
  const std::vector<SecurityPostureViolation> all = {
      SecurityPostureViolation::kRendererSandboxDisabled,
      SecurityPostureViolation::kGpuSandboxDisabled,
      SecurityPostureViolation::kSiteIsolationDisabled,
      SecurityPostureViolation::kSiteIsolationUnknown,
      SecurityPostureViolation::kTlsVerificationRelaxed,
      SecurityPostureViolation::kWebSecurityDisabled,
      SecurityPostureViolation::kSafeBrowsingAbsentFromBuild,
      SecurityPostureViolation::kSafeBrowsingOffByDefault,
      SecurityPostureViolation::kDangerousSwitchPresent,
  };

  for (SecurityPostureViolation value : all) {
    const std::string sentence = DescribeSecurityPostureViolation(value);
    EXPECT_FALSE(sentence.empty()) << "violation bit " << AsMask(value);
    // One sentence a reviewer can act on, not an identifier.
    EXPECT_NE('k', sentence.front());
  }
  EXPECT_TRUE(std::string(DescribeSecurityPostureViolation(
                              SecurityPostureViolation::kNone))
                  .empty());
}

}  // namespace
}  // namespace taffy
