// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/page_capabilities.h"

#include <algorithm>
#include <array>

#include "base/check.h"
#include "taffy/renderer/adapters/adapter_registry.h"

namespace taffy {

namespace {

using Availability = AdapterAvailability;

// Every adapter kind, once, in registry order. Written out rather than
// derived so that adding a kind is a compile error here as well as in the
// registry - a capability report that silently omits a new adapter is exactly
// the dishonesty CAP-PI-008 exists to prevent.
constexpr auto kAllKinds = std::to_array<AdapterKind>({
    AdapterKind::kDocumentMetadata,
    AdapterKind::kAccessibility,
    AdapterKind::kForms,
    AdapterKind::kStructuredData,
    AdapterKind::kDom,
    AdapterKind::kSelection,
    AdapterKind::kLayout,
});

// The extraction rule version each adapter stamps into its evidence. Held
// here so the capability report can state it without constructing an adapter,
// and asserted against the adapters themselves in the unit test - two copies
// that a test proves equal, rather than one copy nothing can read.
uint32_t RuleVersionOf(AdapterKind kind) {
  switch (kind) {
    case AdapterKind::kDom:
      return 2;
    case AdapterKind::kAccessibility:
      return 2;
    case AdapterKind::kForms:
      return 3;
    case AdapterKind::kStructuredData:
      return 1;
    case AdapterKind::kDocumentMetadata:
      return 1;
    case AdapterKind::kSelection:
      return 1;
    case AdapterKind::kLayout:
      return 1;
  }
}

struct Standing {
  Availability availability;
  std::string_view detail_code;
};

// What one adapter can do for this document, from the signals alone. Every
// branch that is not "present" names why: a caller that is told an adapter is
// unsupported and not told why cannot decide whether to narrow its request or
// give up.
Standing StandingFor(AdapterKind kind,
                     const DocumentCapabilitySignals& signals) {
  if (!signals.has_document) {
    return {Availability::kUnsupported, capability_reason::kNoDocument};
  }

  switch (kind) {
    case AdapterKind::kDocumentMetadata:
      if (!signals.has_browser_assigned_epoch) {
        return {Availability::kUnsupported, capability_reason::kNoBrowserEpoch};
      }
      if (!signals.has_browser_origin_set) {
        // The identity comparison this adapter exists to make cannot run.
        // Degraded, not absent: it still emits the document node.
        return {Availability::kDegraded,
                capability_reason::kNoBrowserOriginSet};
      }
      if (!signals.lifecycle_is_active) {
        return {Availability::kDegraded, capability_reason::kDocumentNotActive};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kAccessibility:
      if (!signals.accessibility_available) {
        return {Availability::kUnsupported,
                capability_reason::kAccessibilityUnavailable};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kForms:
      if (!signals.has_form_controls) {
        // A document with no controls is not a document this adapter fails
        // on; it is one it has nothing to say about. Unsupported is the
        // protocol's word for that (section 7.6) and is what makes "zero
        // controls" distinguishable from "we did not look".
        return {Availability::kUnsupported, capability_reason::kNoFormControls};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kStructuredData:
      if (!signals.has_structured_data) {
        return {Availability::kUnsupported,
                capability_reason::kNoStructuredData};
      }
      if (!signals.accessibility_available) {
        // Nothing perceivable to corroborate against, so every candidate
        // would be reported uncorroborated for a reason that has nothing to
        // do with the page.
        return {Availability::kDegraded,
                capability_reason::kAccessibilityUnavailable};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kDom:
      if (!signals.has_body) {
        return {Availability::kUnsupported, capability_reason::kNoBody};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kSelection:
      if (!signals.has_selection) {
        return {Availability::kUnsupported, capability_reason::kNoSelection};
      }
      if (!signals.accessibility_available) {
        // The selection is read through the accessibility tree, so without
        // one there is nothing to scope it to.
        return {Availability::kUnsupported,
                capability_reason::kAccessibilityUnavailable};
      }
      return {Availability::kPresent, std::string_view()};

    case AdapterKind::kLayout:
      if (!signals.viewport_geometry_available) {
        return {Availability::kUnsupported,
                capability_reason::kNoViewportGeometry};
      }
      if (!signals.accessibility_available) {
        // Bounds can still be measured; occlusion cannot be probed, because
        // the probe is an accessibility hit test. Degraded says exactly that,
        // and an action precondition that forbids occlusion is refused for
        // every node - which is the honest outcome, not a convenient one.
        return {Availability::kDegraded,
                capability_reason::kAccessibilityUnavailable};
      }
      return {Availability::kPresent, std::string_view()};
  }
}

// A run can lower a standing and can never raise it: a document that could
// not support an adapter does not become able to because the adapter
// returned something.
Availability Lower(Availability current, Availability observed) {
  if (current == Availability::kUnsupported ||
      observed == Availability::kUnsupported) {
    return Availability::kUnsupported;
  }
  if (current == Availability::kDegraded ||
      observed == Availability::kDegraded) {
    return Availability::kDegraded;
  }
  return Availability::kPresent;
}

Standing StandingForStatus(AdapterStatus status) {
  switch (status) {
    case AdapterStatus::kOk:
      return {Availability::kPresent, std::string_view()};
    case AdapterStatus::kIncomplete:
      return {Availability::kDegraded, capability_reason::kRunIncomplete};
    case AdapterStatus::kConflicted:
      // Conflicted is not a failure: preserving a disagreement is the correct
      // outcome (protocol section 7.5). It is degraded because a caller must
      // not read a conflicted answer as a settled one.
      return {Availability::kDegraded, capability_reason::kRunConflicted};
    case AdapterStatus::kUnsupported:
      return {Availability::kUnsupported, capability_reason::kRunUnsupported};
    case AdapterStatus::kFailed:
      return {Availability::kUnsupported, capability_reason::kRunFailed};
  }
}

}  // namespace

DocumentCapabilitySignals::DocumentCapabilitySignals() = default;
DocumentCapabilitySignals::DocumentCapabilitySignals(
    const DocumentCapabilitySignals&) = default;
DocumentCapabilitySignals& DocumentCapabilitySignals::operator=(
    const DocumentCapabilitySignals&) = default;
DocumentCapabilitySignals::~DocumentCapabilitySignals() = default;

PageCapabilities::PageCapabilities() = default;
PageCapabilities::PageCapabilities(const PageCapabilities&) = default;
PageCapabilities& PageCapabilities::operator=(const PageCapabilities&) =
    default;
PageCapabilities::~PageCapabilities() = default;

// static
PageCapabilities PageCapabilities::ForDocument(
    const DocumentCapabilitySignals& signals) {
  PageCapabilities capabilities;
  capabilities.adapters_.reserve(kAllKinds.size());
  for (AdapterKind kind : kAllKinds) {
    const Standing standing = StandingFor(kind, signals);
    AdapterCapability capability;
    capability.kind = kind;
    capability.name = AdapterRegistry::NameOf(kind);
    capability.extraction_rule_version = RuleVersionOf(kind);
    capability.precedence = AdapterRegistry::PrecedenceOf(kind);
    capability.availability = standing.availability;
    capability.detail_code = standing.detail_code;
    capabilities.adapters_.push_back(capability);
  }
  return capabilities;
}

void PageCapabilities::RecordRunOutcome(AdapterKind kind,
                                        AdapterStatus status) {
  auto it = std::ranges::find_if(adapters_,
                                 [kind](const AdapterCapability& capability) {
                                   return capability.kind == kind;
                                 });
  CHECK(it != adapters_.end());

  const Standing observed = StandingForStatus(status);
  const Availability lowered = Lower(it->availability, observed.availability);
  if (lowered != it->availability && !observed.detail_code.empty()) {
    it->detail_code = observed.detail_code;
  }
  it->availability = lowered;
}

const AdapterCapability& PageCapabilities::For(AdapterKind kind) const {
  auto it = std::ranges::find_if(adapters_,
                                 [kind](const AdapterCapability& capability) {
                                   return capability.kind == kind;
                                 });
  CHECK(it != adapters_.end());
  return *it;
}

bool PageCapabilities::SupportsScope(ExtractionScope scope) const {
  switch (scope) {
    case ExtractionScope::kDocument:
      return AnyProducerAvailable();
    case ExtractionScope::kInteractive:
      // Interactive nodes are the ones with actions, which every producing
      // adapter can state on its own.
      return AnyProducerAvailable();
    case ExtractionScope::kViewport:
      // Deciding what is in the viewport without visibility information would
      // mean guessing, and a viewport-scoped answer that quietly included
      // off-screen decoys is the shape the hidden-and-off-screen fixture is
      // built to catch.
      return AnyProducerAvailable() && For(AdapterKind::kLayout).availability !=
                                           AdapterAvailability::kUnsupported;
    case ExtractionScope::kSelection:
      return For(AdapterKind::kSelection).availability !=
             AdapterAvailability::kUnsupported;
    case ExtractionScope::kSection:
      // BIP 0.9 names one exact live form root on every section request. The
      // per-request root still has to resolve; this capability says only that
      // the document has the adapter that can describe it.
      return For(AdapterKind::kForms).availability !=
             AdapterAvailability::kUnsupported;
  }
}

bool PageCapabilities::AnyProducerAvailable() const {
  return std::ranges::any_of(adapters_, [](const AdapterCapability& c) {
    return c.precedence > 0 &&
           c.availability != AdapterAvailability::kUnsupported;
  });
}

}  // namespace taffy
