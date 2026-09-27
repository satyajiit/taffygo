// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SECURITY_POSTURE_H_
#define TAFFY_BROWSER_SECURITY_POSTURE_H_

#include <stdint.h>

// What the fork did and did not switch off (PAR-SEC-001 and PAR-SEC-008).
//
// The parity rows are stated as prohibitions — no disabling the sandbox, site
// isolation, TLS verification, Safe Browsing or the mitigations, and
// architecture tests confirm the selected fork configuration. A prohibition
// with no observation behind it is a promise. This struct is the observation:
// a set of browser-owned facts, gathered by SecurityPostureProbe, judged by
// SecurityPostureEvaluator, and reported alongside the upstream provenance by
// SecurityPostureReport.
//
// The value of splitting the facts from the judgement is that the judgement is
// a pure function, so the whole compliance table is unit-testable on a laptop
// while the facts need a running browser. That is the same split the fork uses
// everywhere else, and it is why this file contains no logic at all.

namespace taffy {

// How much process separation the browser is actually enforcing.
enum class SiteIsolationMode : uint8_t {
  // Not established. Fails closed: treated as a violation, because "we could
  // not tell" and "it is off" have the same consequence for a reviewer.
  kUnknown = 0,
  kDisabled = 1,
  // Isolated origins only — the memory-constrained Android configuration,
  // where upstream itself isolates a set of origins rather than every site.
  kIsolatedOriginsOnly = 2,
  // A dedicated process for every site.
  kStrictSitePerProcess = 3,
};

// Command-line switches that turn a protection off. Every one of these exists
// upstream for a legitimate testing reason and none of them belongs in a build
// a person uses. A bit field rather than a list so the posture stays a plain
// value.
enum class DangerousSwitch : uint32_t {
  kNone = 0,
  kNoSandbox = 1 << 0,
  kDisableWebSecurity = 1 << 1,
  kDisableSiteIsolation = 1 << 2,
  kIgnoreCertificateErrors = 1 << 3,
  kIgnoreCertificateErrorsSpkiList = 1 << 4,
  kAllowRunningInsecureContent = 1 << 5,
  kDisableGpuSandbox = 1 << 6,
  kTreatInsecureOriginAsSecure = 1 << 7,
  kDisableWebSecurityForTesting = 1 << 8,
};

constexpr uint32_t AsMask(DangerousSwitch value) {
  return static_cast<uint32_t>(value);
}

constexpr bool HasDangerousSwitch(uint32_t mask, DangerousSwitch value) {
  return (mask & AsMask(value)) != 0;
}

struct SecurityPosture {
  // The renderer sandbox. False is the single most serious finding this file
  // can carry.
  bool renderer_sandbox_enabled = false;
  bool gpu_sandbox_enabled = false;

  SiteIsolationMode site_isolation = SiteIsolationMode::kUnknown;

  // True when the network stack is verifying certificates normally — no
  // ignored errors, no injected trust anchors, no allow-list of public key
  // hashes.
  bool tls_certificate_verification_enforced = false;

  // True when Blink's web security — the same-origin policy and everything
  // built on it — is on.
  bool web_security_enabled = false;

  // Whether Safe Browsing is compiled into this build at all, and whether it
  // is on by default for a fresh profile. The two are separate findings: a
  // build without it cannot be configured to have it, while a build with it
  // switched off by default is a settings decision that needs a decision
  // record.
  bool safe_browsing_available_in_build = false;
  bool safe_browsing_enabled_by_default = false;

  // True when the binary resolves no metrics upload endpoint — the unbranded
  // state, where the public tree's server URLs are placeholders that resolve
  // empty. Decision 0019 records the UMA/UKM uploads as "inert without keys
  // or branding" on exactly this fact, so a build where an endpoint appears
  // (an internal grd graft, a branding change) must surface as a finding
  // rather than silently reviving the upload path. Defaults false: fail
  // closed, like every other field here.
  bool metrics_upload_endpoint_absent = false;

  // Bit field of DangerousSwitch values seen on the browser process command
  // line.
  uint32_t dangerous_switch_mask = AsMask(DangerousSwitch::kNone);

  // True when the binary was built with a sanitizer or with the test
  // scaffolding that relaxes checks. Not a violation on its own — the value of
  // recording it is that a posture gathered from such a build must never be
  // published as evidence for a shipping one.
  bool is_instrumented_build = false;

  friend bool operator==(const SecurityPosture&,
                         const SecurityPosture&) = default;
};

// Each violation is one sentence a reviewer can act on. The numbering is
// stable because it appears in reports.
enum class SecurityPostureViolation : uint32_t {
  kNone = 0,
  kRendererSandboxDisabled = 1 << 0,
  kGpuSandboxDisabled = 1 << 1,
  kSiteIsolationDisabled = 1 << 2,
  kSiteIsolationUnknown = 1 << 3,
  kTlsVerificationRelaxed = 1 << 4,
  kWebSecurityDisabled = 1 << 5,
  kSafeBrowsingAbsentFromBuild = 1 << 6,
  kSafeBrowsingOffByDefault = 1 << 7,
  kDangerousSwitchPresent = 1 << 8,
  kMetricsUploadEndpointPresent = 1 << 9,
};

constexpr uint32_t AsMask(SecurityPostureViolation value) {
  return static_cast<uint32_t>(value);
}

constexpr bool HasViolation(uint32_t mask, SecurityPostureViolation value) {
  return (mask & AsMask(value)) != 0;
}

struct SecurityPostureVerdict {
  uint32_t violation_mask = AsMask(SecurityPostureViolation::kNone);

  // True when nothing at all was found. Stored rather than derived so a
  // consumer cannot get the "is the mask empty" test subtly wrong.
  bool is_compliant = false;

  // True when at least one finding is of the kind that makes a build unfit to
  // hand to a person, as opposed to a configuration question. The sandbox,
  // site isolation, TLS and web security are in this set; the Safe Browsing
  // default is not, because turning it off is a decision somebody can record
  // and a disabled sandbox is not.
  bool has_disqualifying_finding = false;

  friend bool operator==(const SecurityPostureVerdict&,
                         const SecurityPostureVerdict&) = default;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_SECURITY_POSTURE_H_
