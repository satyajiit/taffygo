// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/security_posture_probe.h"

#include "base/command_line.h"
#include "components/metrics/server_urls.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/safe_browsing/buildflags.h"
#include "taffy/browser/security_posture_assertions.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/common/content_switches.h"
#include "sandbox/policy/switches.h"
#include "services/network/public/cpp/network_switches.h"

namespace taffy {

namespace {

SecurityPostureEmbedderSource* g_embedder_source = nullptr;

// Each row is one upstream switch and the bit it sets. Written as data so that
// adding a switch is a one-line change a reviewer can read, and so the unit
// test can walk the same list the probe walks.
//
// VERIFIED AT 152.0.7977.42: every switch constant below.
// Upstream files read:
//   sandbox/policy/switches.h            — kNoSandbox, kDisableGpuSandbox
//   content/public/common/content_switches.h
//                                        — kDisableWebSecurity,
//                                          kDisableSiteIsolation
//   components/network_session_configurator/common/network_switches.h
//                                        — kIgnoreCertificateErrors. Still
//                                          namespace-scope `switches::`, but it
//                                          is no longer content's: the
//                                          declaration is generated from
//                                          network_switch_list.h in the
//                                          network_session_configurator
//                                          component, because that component is
//                                          what parses the flag.
//   services/network/public/cpp/network_switches.h
//                                        — kIgnoreCertificateErrorsSPKIList,
//                                          kUnsafelyTreatInsecureOriginAsSecure
// A constant that moved is a compile error here, which is the failure mode
// worth having: the check stops the build rather than quietly reporting a
// clean posture.
struct SwitchRow {
  const char* switch_name;
  DangerousSwitch bit;
};

// The one row that cannot be spelled as an upstream constant. At
// 152.0.7977.42 `--allow-running-insecure-content` is defined only in
// chrome/common/chrome_switches.cc; content no longer declares it, because the
// flag is turned into a content setting by the browser layer rather than parsed
// by //content. This directory's DEPS forbids +chrome on purpose — a component
// that reaches up into //chrome cannot be reused or tested on its own — so the
// choice is between naming the flag here and dropping the row, and dropping it
// would make the probe report a clean posture on a command line that has mixed
// content switched on. What is spelled here is the flag a person types, which
// upstream cannot change without a deprecation cycle; what was lost is the
// compile-time canary, and only for this one row. The durable home for a fact
// that now lives above //components is SecurityPostureEmbedderSource, which
// exists for exactly that reason — a move this row cannot make by itself,
// because it changes the probe's header and every embedder that implements it.
constexpr char kAllowRunningInsecureContentSwitch[] =
    "allow-running-insecure-content";

const SwitchRow kDangerousSwitches[] = {
    {sandbox::policy::switches::kNoSandbox, DangerousSwitch::kNoSandbox},
    {sandbox::policy::switches::kDisableGpuSandbox,
     DangerousSwitch::kDisableGpuSandbox},
    {switches::kDisableWebSecurity, DangerousSwitch::kDisableWebSecurity},
    {switches::kDisableSiteIsolation, DangerousSwitch::kDisableSiteIsolation},
    {switches::kIgnoreCertificateErrors,
     DangerousSwitch::kIgnoreCertificateErrors},
    {kAllowRunningInsecureContentSwitch,
     DangerousSwitch::kAllowRunningInsecureContent},
    {network::switches::kIgnoreCertificateErrorsSPKIList,
     DangerousSwitch::kIgnoreCertificateErrorsSpkiList},
    {network::switches::kUnsafelyTreatInsecureOriginAsSecure,
     DangerousSwitch::kTreatInsecureOriginAsSecure},
};

uint32_t GatherDangerousSwitchMask(const base::CommandLine& command_line) {
  uint32_t mask = AsMask(DangerousSwitch::kNone);
  for (const SwitchRow& row : kDangerousSwitches) {
    if (command_line.HasSwitch(row.switch_name)) {
      mask |= AsMask(row.bit);
    }
  }
  return mask;
}

SiteIsolationMode GatherSiteIsolationMode() {
  // VERIFY AT SP-01: content::SiteIsolationPolicy at the pinned milestone.
  // Upstream file to read: content/public/browser/site_isolation_policy.h —
  // UseDedicatedProcessesForAllSites() and AreDynamicIsolatedOriginsEnabled().
  // Both are stable, browser-process, static predicates. If either moved, the
  // fact still exists somewhere in that class and this is the only call site.
  if (content::SiteIsolationPolicy::UseDedicatedProcessesForAllSites()) {
    return SiteIsolationMode::kStrictSitePerProcess;
  }

  // Partial site isolation: a process per site is not in force, but nothing
  // switched isolation off either, so origins are still isolated one at a
  // time — the preloaded list, plus the ones the browser adds at run time when
  // it sees a password field, an OAuth site or a COOP header. This is the
  // configuration every Android phone the product targets actually runs in.
  //
  // The question asked here is AreDynamicIsolatedOriginsEnabled() and not
  // AreIsolatedOriginsEnabled(), which is what this function asked until it
  // was corrected, and the difference between them is the whole defect.
  // VERIFIED AT 152.0.7977.42:
  //
  //   * AreIsolatedOriginsEnabled() is true only for --isolate-origins or a
  //     force-enabled features::kIsolateOrigins, and that feature is
  //     FEATURE_DISABLED_BY_DEFAULT
  //     (content/public/common/content_features.cc). Neither is how Chromium
  //     isolates origins on Android, so the answer is false on a phone that is
  //     isolating origins perfectly well.
  //   * AreDynamicIsolatedOriginsEnabled() is
  //     !IsSiteIsolationDisabled(kPartialSiteIsolation): false only when
  //     --disable-site-isolation or --disable-site-isolation-for-policy is
  //     present, or when the embedder disables it — the Android memory
  //     threshold in components/site_isolation/site_isolation_policy.cc. It is
  //     the predicate upstream's own partial-isolation callers gate on
  //     (IsIsolationForPasswordSitesEnabled, IsIsolationForOAuthSitesEnabled,
  //     content's own IsSiteIsolationForCOOPEnabled), which is what makes it
  //     the answer to "is partial site isolation in force".
  //
  // Asking the first question reported kDisabled for a browser isolating
  // origins normally, and the evaluator turns kDisabled into a disqualifying
  // finding — the exact outcome the evaluator's kIsolatedOriginsOnly comment
  // says must not happen, on every device the product ships to. It also had a
  // side effect a read-only probe must not have: consulting kIsolateOrigins
  // activates that field trial and fixes the client's group assignment.
  if (content::SiteIsolationPolicy::AreDynamicIsolatedOriginsEnabled()) {
    return SiteIsolationMode::kIsolatedOriginsOnly;
  }

  // Site isolation is genuinely off — a switch, an enterprise policy, or a
  // device under the embedder's memory threshold. All three are true findings.
  return SiteIsolationMode::kDisabled;
}

}  // namespace

// static
void SecurityPostureProbe::SetEmbedderSource(
    SecurityPostureEmbedderSource* source) {
  g_embedder_source = source;
}

// static
SecurityPosture SecurityPostureProbe::Gather() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  SecurityPosture posture;
  posture.dangerous_switch_mask = GatherDangerousSwitchMask(command_line);
  posture.site_isolation = GatherSiteIsolationMode();

  // The sandbox facts are derived from the switches rather than from a runtime
  // query, deliberately. A runtime "am I sandboxed" answer differs per process
  // and this code runs in the browser process, which is not sandboxed by
  // design; what the parity row asks is whether the *build* disabled the
  // sandbox for the processes that should have one, and the switch is that
  // fact.
  posture.renderer_sandbox_enabled = !HasDangerousSwitch(
      posture.dangerous_switch_mask, DangerousSwitch::kNoSandbox);
  posture.gpu_sandbox_enabled =
      posture.renderer_sandbox_enabled &&
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kDisableGpuSandbox);

  posture.tls_certificate_verification_enforced =
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kIgnoreCertificateErrors) &&
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kIgnoreCertificateErrorsSpkiList) &&
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kTreatInsecureOriginAsSecure);

  posture.web_security_enabled =
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kDisableWebSecurity) &&
      !HasDangerousSwitch(posture.dangerous_switch_mask,
                          DangerousSwitch::kAllowRunningInsecureContent);

  posture.safe_browsing_available_in_build =
      BUILDFLAG(SAFE_BROWSING_AVAILABLE);
  posture.safe_browsing_enabled_by_default =
      g_embedder_source ? g_embedder_source->IsSafeBrowsingEnabledByDefault()
                        : false;

  // Decision 0019's "inert without branding" tripwire. In the public tree
  // the metrics and UKM server URLs are grd placeholders that resolve to an
  // empty GURL (components/metrics/server_urls.cc, VERIFIED AT 152.0.7977.42);
  // an internal-grd graft or a branding change makes one non-empty, and this
  // is where that surfaces as a finding instead of a silent revival.
  posture.metrics_upload_endpoint_absent =
      metrics::GetMetricsServerUrl().is_empty() &&
      metrics::GetUkmServerUrl().is_empty();

  posture.is_instrumented_build = kIsInstrumentedBuild;
  return posture;
}

}  // namespace taffy
