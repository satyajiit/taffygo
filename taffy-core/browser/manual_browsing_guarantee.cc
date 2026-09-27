// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/manual_browsing_guarantee.h"

#include <string>

#include "base/no_destructor.h"
#include "taffy/browser/ai_runtime_availability.h"
#include "taffy/browser/credential_boundary.h"
#include "taffy/browser/navigation_error_classifier.h"
#include "taffy/browser/omnibox_input_classifier.h"
#include "taffy/components/intelligence/content/scrubbing_serializer.h"
#include "taffy/browser/security_posture_evaluator.h"
#include "content/public/browser/browser_thread.h"
#include "net/base/net_errors.h"

namespace taffy {

namespace {

// The built-in probes. Each one exercises its seam for real and checks the
// answer, so a probe reporting kOperational means the capability worked, not
// that it was present.

class AddressBarProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    // PAR-BOX-001's two halves: a place is recognised, and an ambiguous input
    // asks rather than guessing.
    const OmniboxClassification url =
        OmniboxInputClassifier::Classify("example.com");
    if (url.kind != OmniboxInputKind::kUrlWithImplicitScheme) {
      return ManualCapabilityStatus::kFailed;
    }
    const OmniboxClassification query =
        OmniboxInputClassifier::Classify("best desk lamp");
    if (query.kind != OmniboxInputKind::kSearchQuery) {
      return ManualCapabilityStatus::kFailed;
    }
    const OmniboxClassification ambiguous =
        OmniboxInputClassifier::Classify("server.internal");
    if (!ambiguous.requires_user_choice) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }
};

class NavigationErrorProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    // PAR-NAV-006: an offline failure is recognised as one, and it does not
    // read as content.
    NavigationErrorFacts facts;
    facts.net_error = net::ERR_INTERNET_DISCONNECTED;
    facts.is_error_page = true;
    const NavigationErrorVerdict verdict = ClassifyNavigationError(facts);
    if (verdict.error_class != NavigationErrorClass::kOffline ||
        !verdict.content_is_absent) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }
};

class CredentialIsolationProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    // PAR-AUTH-001: a credential interaction suspends assistant access, and
    // ending it resumes. Uses its own boundary rather than the process-wide
    // one so that probing never disturbs a live tab.
    CredentialBoundary boundary;
    const TabId tab{"manual_browsing_guarantee_probe"};
    if (!boundary.AssistantMayObserve(tab)) {
      return ManualCapabilityStatus::kFailed;
    }
    boundary.BeginCredentialInteraction(tab, CredentialClass::kPassword);
    if (boundary.AssistantMayObserve(tab)) {
      return ManualCapabilityStatus::kFailed;
    }
    boundary.EndCredentialInteraction(tab, CredentialClass::kPassword);
    if (!boundary.AssistantMayObserve(tab)) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }
};

class SecurityPostureProbeForGuarantee : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    // PAR-SEC-001: the evaluator still judges. Gathering the live posture is
    // deliberately not done here — the probe must be identical in every
    // runtime state, and the live posture is a property of the process rather
    // than of the seam.
    SecurityPosture disabled;
    if (EvaluateSecurityPosture(disabled).is_compliant) {
      return ManualCapabilityStatus::kFailed;
    }
    SecurityPosture clean;
    clean.renderer_sandbox_enabled = true;
    clean.gpu_sandbox_enabled = true;
    clean.site_isolation = SiteIsolationMode::kStrictSitePerProcess;
    clean.tls_certificate_verification_enforced = true;
    clean.web_security_enabled = true;
    clean.safe_browsing_available_in_build = true;
    clean.safe_browsing_enabled_by_default = true;
    clean.metrics_upload_endpoint_absent = true;
    if (!EvaluateSecurityPosture(clean).is_compliant) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }
};

class DiagnosticScrubbingProbe : public ManualCapabilityProbe {
 public:
  ManualCapabilityStatus Probe() override {
    // PAR-SEC-009: the gate still removes what it is for. The value used here
    // is a shape, not a fixture canary: a production binary must not contain
    // the tokens its own leak detector looks for.
    const ScrubbedText scrubbed =
        ScrubbingSerializer::Serialize("password=probe-value-not-a-secret");
    if (scrubbed.value().find("probe-value-not-a-secret") !=
        std::string::npos) {
      return ManualCapabilityStatus::kFailed;
    }
    if (!scrubbed.report().anything_was_removed()) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }
};

AddressBarProbe& AddressBar() {
  static base::NoDestructor<AddressBarProbe> probe;
  return *probe;
}
NavigationErrorProbe& NavigationErrors() {
  static base::NoDestructor<NavigationErrorProbe> probe;
  return *probe;
}
CredentialIsolationProbe& CredentialIsolation() {
  static base::NoDestructor<CredentialIsolationProbe> probe;
  return *probe;
}
SecurityPostureProbeForGuarantee& PostureEvaluation() {
  static base::NoDestructor<SecurityPostureProbeForGuarantee> probe;
  return *probe;
}
DiagnosticScrubbingProbe& DiagnosticScrubbing() {
  static base::NoDestructor<DiagnosticScrubbingProbe> probe;
  return *probe;
}

}  // namespace

ManualBrowsingGuarantee::ManualBrowsingGuarantee() {
  // The pure seams can be exercised for real with no platform behind them, so
  // they are always registered. The rest arrive from the layer that owns their
  // delegate.
  probes_[ManualCapability::kAddressBar] = &AddressBar();
  probes_[ManualCapability::kNavigation] = &NavigationErrors();
  probes_[ManualCapability::kCredentialIsolation] = &CredentialIsolation();
  probes_[ManualCapability::kSecurityPosture] = &PostureEvaluation();
  probes_[ManualCapability::kDiagnosticScrubbing] = &DiagnosticScrubbing();
}

ManualBrowsingGuarantee::~ManualBrowsingGuarantee() = default;

// static
ManualBrowsingGuarantee& ManualBrowsingGuarantee::Get() {
  static base::NoDestructor<ManualBrowsingGuarantee> instance;
  return *instance;
}

void ManualBrowsingGuarantee::Register(ManualCapability capability,
                                       ManualCapabilityProbe* probe) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!probe) {
    probes_.erase(capability);
    return;
  }
  probes_[capability] = probe;
}

ManualBrowsingVerdict ManualBrowsingGuarantee::Verify() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  AiRuntimeAvailability& availability = AiRuntimeAvailability::Get();
  const AiRuntimeState original = availability.state();

  ManualBrowsingVerdict verdict;
  verdict.registered_capability_count = probes_.size();
  verdict.manual_browsing_is_independent = true;

  for (const auto& [capability, probe] : probes_) {
    ManualCapabilityFinding finding;
    finding.capability = capability;

    bool first = true;
    ManualCapabilityStatus reference = ManualCapabilityStatus::kOperational;

    for (AiRuntimeState state : kAllAiRuntimeStates) {
      // Actually set it. A probe that consults the runtime — directly, or
      // through some intermediary that does — answers differently here, and
      // the difference is the finding.
      availability.SetState(state);
      const ManualCapabilityStatus status = probe->Probe();

      if (first) {
        reference = status;
        first = false;
      } else if (status != reference) {
        finding.varies_with_runtime_state = true;
      }
      if (status == ManualCapabilityStatus::kFailed) {
        finding.failed_in_some_state = true;
      }
      if (state == AiRuntimeState::kReady) {
        finding.with_runtime_ready = status;
      }
      if (state == AiRuntimeState::kAbsent) {
        finding.with_runtime_absent = status;
      }
    }

    if (finding.varies_with_runtime_state || finding.failed_in_some_state) {
      verdict.manual_browsing_is_independent = false;
    }
    verdict.findings.push_back(finding);
  }

  availability.SetState(original);
  return verdict;
}

}  // namespace taffy
