// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_PAGE_CAPABILITIES_H_
#define TAFFY_RENDERER_PAGE_CAPABILITIES_H_

#include <stdint.h>

#include <string_view>
#include <vector>

#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/semantic_graph.h"

// CAP-PI-008. An honest account of which adapters are present, degraded, or
// unsupported FOR THIS DOCUMENT.
//
// Protocol section 6.2 makes this load-bearing rather than informational: "a
// request naming a capability the endpoint does not support returns
// UNSUPPORTED, never a degraded result that looks complete". A caller reads
// this before it asks for anything, so an endpoint that over-reports here
// causes exactly the failure the rule exists to prevent - a snapshot that
// looks whole and is not.
//
// Three things make this a module rather than a few lines inside the
// endpoint:
//
//   1. It is computed per document, not per build. Whether the selection
//      adapter can say anything depends on whether the user has selected
//      something; whether the layout adapter can depends on whether this
//      frame has widget geometry. Reporting the build-time adapter list would
//      be reporting what the binary contains rather than what this document
//      supports.
//
//   2. Every adapter it reports can be named on the wire, and each one is
//      reported under its own member. That was not always true: selection and
//      layout had no mojom::AdapterKind member, so this module marked them as
//      lacking a wire identity and the endpoint described them in a warning
//      rather than under a member that means something else. The additive
//      minor contract step recorded in
//      taffy-core/contracts/bip/schema/bip.version.json appended SELECTION and
//      LAYOUT, so the marking and the warning path are both gone and a caller
//      reads one kind of row for every adapter.
//
//   3. It is host-testable. No Blink type appears here: the endpoint collects
//      the signals and hands them in, so what a document supports is decided
//      by a pure function that a unit test can drive through every
//      combination.

namespace taffy {

enum class AdapterAvailability {
  // The adapter can run against this document and produced or would produce a
  // complete answer.
  kPresent,
  // It ran, or can run, but its answer for this document is partial - a
  // budget was reached, a check could not be performed, or sources
  // disagreed. A caller must not treat a degraded adapter's silence as
  // absence of content.
  kDegraded,
  // It cannot serve this document at all. A request that names it as
  // required is UNSUPPORTED.
  kUnsupported,
};

// The document facts that decide what can run. Collected by the endpoint from
// Blink and from what the browser stated, and passed in by value so this
// header stays free of renderer types.
struct DocumentCapabilitySignals {
  DocumentCapabilitySignals();
  DocumentCapabilitySignals(const DocumentCapabilitySignals&);
  DocumentCapabilitySignals& operator=(const DocumentCapabilitySignals&);
  ~DocumentCapabilitySignals();

  // False when there is no document to observe at all.
  bool has_document = false;
  // The browser assigned an epoch for this observation. Without one the
  // document metadata adapter has no identity to report and refuses.
  bool has_browser_assigned_epoch = false;
  // The browser stated an origin set to compare against.
  bool has_browser_origin_set = false;
  // The browser calls this document active.
  bool lifecycle_is_active = false;
  // An accessibility context can be built for this document.
  bool accessibility_available = false;
  // The frame has widget geometry, without which nothing can be called
  // on-screen or off-screen.
  bool viewport_geometry_available = false;
  // The user currently has something selected.
  bool has_selection = false;
  // The document contains at least one form control, inside a form or not.
  bool has_form_controls = false;
  // The document contains at least one JSON-LD block or microdata attribute.
  bool has_structured_data = false;
  // The document has a body to walk.
  bool has_body = false;
};

// One adapter's standing for this document.
struct AdapterCapability {
  AdapterKind kind = AdapterKind::kDom;
  std::string_view name;
  uint32_t extraction_rule_version = 0;
  // Protocol section 7.6 precedence, zero for annotators.
  uint32_t precedence = 0;
  AdapterAvailability availability = AdapterAvailability::kUnsupported;
  // A stable code from a closed local set explaining a degraded or
  // unsupported standing. Empty when present. Never prose, never page
  // content, never user-facing text.
  std::string_view detail_code;
};

class PageCapabilities {
 public:
  PageCapabilities();
  PageCapabilities(const PageCapabilities&);
  PageCapabilities& operator=(const PageCapabilities&);
  ~PageCapabilities();

  // What this document supports, before anything has run.
  static PageCapabilities ForDocument(const DocumentCapabilitySignals& signals);

  // Folds an actual run's outcome into the report. An adapter that reported
  // kOk stays present; anything else can only ever lower its standing, never
  // raise it - a run cannot prove a capability the document did not have.
  void RecordRunOutcome(AdapterKind kind, AdapterStatus status);

  const std::vector<AdapterCapability>& adapters() const { return adapters_; }

  // The capability for one kind. Every kind always has an entry: an absent
  // adapter is reported as unsupported, not omitted, because omission reads
  // as "not asked about".
  const AdapterCapability& For(AdapterKind kind) const;

  // Whether a scope can be served for this document. kSelection needs the
  // selection adapter; kViewport needs the layout adapter, because deciding
  // what is in the viewport without visibility information would mean
  // guessing. kSection is the contract's typed form-root scope and therefore
  // needs the forms adapter; SnapshotBuilder separately validates the exact
  // live root named by each request.
  bool SupportsScope(ExtractionScope scope) const;

  // True when at least one adapter can produce nodes for this document.
  bool AnyProducerAvailable() const;

 private:
  std::vector<AdapterCapability> adapters_;
};

// Stable, closed reason codes. Kept in one place so the code an adapter
// report carries and the code a unit test asserts cannot drift apart.
namespace capability_reason {

inline constexpr std::string_view kNoDocument = "no-document";
inline constexpr std::string_view kNoBody = "no-body";
inline constexpr std::string_view kNoBrowserEpoch = "no-browser-assigned-epoch";
inline constexpr std::string_view kNoBrowserOriginSet =
    "no-browser-origin-set-to-compare";
inline constexpr std::string_view kDocumentNotActive = "document-not-active";
inline constexpr std::string_view kAccessibilityUnavailable =
    "accessibility-tree-unavailable";
inline constexpr std::string_view kNoViewportGeometry = "no-viewport-geometry";
inline constexpr std::string_view kNoSelection = "no-selection";
inline constexpr std::string_view kNoFormControls = "no-form-controls";
inline constexpr std::string_view kNoStructuredData = "no-structured-data";
inline constexpr std::string_view kRunIncomplete = "run-incomplete";
inline constexpr std::string_view kRunConflicted = "run-conflicted";
inline constexpr std::string_view kRunUnsupported = "run-unsupported";
inline constexpr std::string_view kRunFailed = "run-failed";

}  // namespace capability_reason

}  // namespace taffy

#endif  // TAFFY_RENDERER_PAGE_CAPABILITIES_H_
