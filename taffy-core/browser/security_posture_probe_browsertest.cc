// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_probe.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "taffy/browser/security_posture_evaluator.h"
#include "taffy/browser/security_posture_report.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/base/ui_base_paths.h"

// PAR-SEC-001 and PAR-SEC-008 against a running browser: the architecture test
// that confirms the selected fork configuration.
//
// This is the test the parity row means by "architecture tests confirm". It
// runs in a real browser process with a real command line and a real
// SiteIsolationPolicy, and it fails if a build argument, a patch or a field
// trial configuration switched a protection off.
//
// Note what the second test does *not* do: it asserts that the browser test
// harness's own relaxations are visible in the posture rather than pretending
// they are absent. A test that reported a clean posture from a harness that
// had disabled the sandbox would be proving nothing at all.

namespace taffy {
namespace {

class TestEmbedderSource : public SecurityPostureEmbedderSource {
 public:
  bool IsSafeBrowsingEnabledByDefault() override { return enabled; }
  bool enabled = true;
};

class SecurityPostureProbeBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();

    // The metrics tripwire (decision 0019) reads IDS_METRICS_SERVER_URL, and
    // that string lives in components_resources, which content_shell.pak does
    // not carry — the shell's ShellMainDelegate loads exactly one pak. Without
    // this the probe takes ResourceBundle's CHECK ("Unable to find resource")
    // and the test crashes rather than fails, which says nothing about the
    // posture. //components:components_tests_pak is a data_dep of
    // //taffy:taffy_browsertests so the file is beside the binary;
    // the two-path lookup and the kScaleFactorNone are the same shape
    // components/test/components_test_suite.cc uses.
    base::FilePath pak_dir;
#if BUILDFLAG(IS_ANDROID)
    ASSERT_TRUE(base::PathService::Get(ui::DIR_RESOURCE_PAKS_ANDROID,
                                       &pak_dir));
#else
    ASSERT_TRUE(base::PathService::Get(base::DIR_ASSETS, &pak_dir));
#endif
    ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        pak_dir.AppendASCII("components_tests_resources.pak"),
        ui::kScaleFactorNone);

    SecurityPostureProbe::SetEmbedderSource(&source_);
  }
  void TearDownOnMainThread() override {
    SecurityPostureProbe::SetEmbedderSource(nullptr);
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  TestEmbedderSource source_;
};

IN_PROC_BROWSER_TEST_F(SecurityPostureProbeBrowserTest,
                       TheBuildDidNotDisableAnyProtection) {
  const SecurityPosture posture = SecurityPostureProbe::Gather();

  // The four the parity rows name, each asserted on its own so that a failure
  // says which one.
  EXPECT_TRUE(posture.renderer_sandbox_enabled)
      << "The renderer sandbox is off. PAR-SEC-001 requires an approved "
         "decision record and threat review before a Chromium protection is "
         "removed.";
  EXPECT_NE(SiteIsolationMode::kDisabled, posture.site_isolation)
      << "Site isolation is disabled. PAR-SEC-008 requires the architecture "
         "test to confirm the selected fork configuration.";
  EXPECT_NE(SiteIsolationMode::kUnknown, posture.site_isolation)
      << "The site isolation mode could not be established, which a reviewer "
         "has to treat the same way as disabled.";
  EXPECT_TRUE(posture.tls_certificate_verification_enforced)
      << "Certificate verification is relaxed.";
  EXPECT_TRUE(posture.web_security_enabled) << "Web security is off.";
  EXPECT_TRUE(posture.safe_browsing_available_in_build)
      << "Safe Browsing is not compiled into this build; see OD-053.";
}

IN_PROC_BROWSER_TEST_F(SecurityPostureProbeBrowserTest,
                       TheProbeReportsSwitchesThatAreActuallyPresent) {
  // The probe must report what is on the command line rather than what the
  // product wishes were on it. Asserting the agreement between the two is what
  // makes the first test meaningful: a probe that always returned a clean
  // posture would pass that one too.
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const SecurityPosture posture = SecurityPostureProbe::Gather();

  EXPECT_EQ(command_line.HasSwitch("no-sandbox"),
            HasDangerousSwitch(posture.dangerous_switch_mask,
                               DangerousSwitch::kNoSandbox));
  EXPECT_EQ(command_line.HasSwitch("disable-web-security"),
            HasDangerousSwitch(posture.dangerous_switch_mask,
                               DangerousSwitch::kDisableWebSecurity));
  EXPECT_EQ(command_line.HasSwitch("ignore-certificate-errors"),
            HasDangerousSwitch(posture.dangerous_switch_mask,
                               DangerousSwitch::kIgnoreCertificateErrors));
}

IN_PROC_BROWSER_TEST_F(SecurityPostureProbeBrowserTest,
                       TheProbeAgreesWithSiteIsolationPolicy) {
  const SecurityPosture posture = SecurityPostureProbe::Gather();

  // The three states, in the order the probe decides them. The middle question
  // is AreDynamicIsolatedOriginsEnabled() — "partial site isolation is in
  // force" — rather than AreIsolatedOriginsEnabled(), which only answers
  // whether --isolate-origins or the IsolateOrigins feature was used and is
  // therefore false on every Android configuration that isolates origins the
  // normal way. See the comment in security_posture_probe.cc; this mirror is
  // only worth having if it mirrors the predicate the probe actually uses.
  if (content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites()) {
    EXPECT_EQ(SiteIsolationMode::kStrictSitePerProcess,
              posture.site_isolation);
  } else if (content::SiteIsolationPolicy::AreDynamicIsolatedOriginsEnabled()) {
    EXPECT_EQ(SiteIsolationMode::kIsolatedOriginsOnly, posture.site_isolation);
  } else {
    EXPECT_EQ(SiteIsolationMode::kDisabled, posture.site_isolation);
  }
}

IN_PROC_BROWSER_TEST_F(SecurityPostureProbeBrowserTest,
                       TheReportJoinsProvenanceAndPosture) {
  // PAR-SEC-002 and PAR-SEC-001 answered from one place, which is the whole
  // reason the report exists.
  const SecurityPostureReport report =
      BuildSecurityPostureReport(SecurityPostureProbe::Gather());

  EXPECT_FALSE(report.chromium_tag.empty());
  EXPECT_FALSE(report.chromium_commit.empty());
  EXPECT_EQ(EvaluateSecurityPosture(report.posture), report.verdict);
}

IN_PROC_BROWSER_TEST_F(SecurityPostureProbeBrowserTest,
                       TheSafeBrowsingDefaultComesFromTheEmbedder) {
  source_.enabled = false;
  EXPECT_FALSE(SecurityPostureProbe::Gather().safe_browsing_enabled_by_default);

  source_.enabled = true;
  EXPECT_TRUE(SecurityPostureProbe::Gather().safe_browsing_enabled_by_default);

  // With no source at all the answer is false, which produces a finding rather
  // than silence.
  SecurityPostureProbe::SetEmbedderSource(nullptr);
  EXPECT_FALSE(SecurityPostureProbe::Gather().safe_browsing_enabled_by_default);
  SecurityPostureProbe::SetEmbedderSource(&source_);
}

}  // namespace
}  // namespace taffy
