// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/wire_conversions.h"

#include "base/notreached.h"

// The evidence and endpoint half of the enumeration translation: where a
// value came from, which adapter produced it, how sensitive it is, what was
// truncated, and what the endpoint reports about itself.
//
// Every function here is a switch with no default case, and that is the only
// reason this file is worth reading: a member added to the mojom, or to the
// internal mirror in semantic_graph.h, becomes a compile error rather than a
// silent fall-through. For an action type or a sensitivity class, a silent
// fall-through is a security bug rather than a defect.
//
// The struct conversions live in wire_struct_conversions.cc. They are a
// different job - they apply the redaction gate and the URL disclosure rule -
// and mixing them in here buried the exhaustiveness property under several
// hundred lines of field copying.

namespace taffy::wire {

mojom::SourceKind ToMojom(SourceKind kind) {
  switch (kind) {
    case SourceKind::kDom:
      return mojom::SourceKind::kDom;
    case SourceKind::kAccessibility:
      return mojom::SourceKind::kAccessibility;
    case SourceKind::kFormControl:
      return mojom::SourceKind::kFormControl;
    case SourceKind::kJsonLd:
      return mojom::SourceKind::kJsonLd;
    case SourceKind::kMicrodata:
      return mojom::SourceKind::kMicrodata;
    case SourceKind::kBrowser:
      return mojom::SourceKind::kBrowser;
    case SourceKind::kAdapter:
      return mojom::SourceKind::kAdapter;
  }
}

mojom::AdapterKind ToMojom(AdapterKind kind) {
  switch (kind) {
    case AdapterKind::kDom:
      return mojom::AdapterKind::kDom;
    case AdapterKind::kAccessibility:
      return mojom::AdapterKind::kAccessibility;
    case AdapterKind::kForms:
      return mojom::AdapterKind::kForms;
    case AdapterKind::kStructuredData:
      // METADATA is what the closed wire enumeration calls "structured
      // metadata for entity attributes" - precedence item 4 of protocol
      // section 7.6 - which is exactly this adapter.
      return mojom::AdapterKind::kMetadata;
    case AdapterKind::kDocumentMetadata:
      // BROWSER is precedence item 1: browser-owned committed navigation and
      // lifecycle state for URL, origin, and document identity.
      return mojom::AdapterKind::kBrowser;
    case AdapterKind::kSelection:
      // SELECTION and LAYOUT were appended to the wire enumeration by the
      // additive minor contract step recorded in
      // taffy-core/contracts/bip/schema/bip.version.json. Before it, these two returned
      // nullopt and the endpoint described them in a warning, because
      // reporting one of them under a member that means something else would
      // have been worse than reporting nothing: a consumer would believe an
      // adapter it never got had run.
      return mojom::AdapterKind::kSelection;
    case AdapterKind::kLayout:
      return mojom::AdapterKind::kLayout;
  }
}

std::optional<AdapterKind> FromMojom(mojom::AdapterKind kind) {
  switch (kind) {
    case mojom::AdapterKind::kDom:
      return AdapterKind::kDom;
    case mojom::AdapterKind::kAccessibility:
      return AdapterKind::kAccessibility;
    case mojom::AdapterKind::kForms:
      return AdapterKind::kForms;
    case mojom::AdapterKind::kMetadata:
      return AdapterKind::kStructuredData;
    case mojom::AdapterKind::kBrowser:
      return AdapterKind::kDocumentMetadata;
    case mojom::AdapterKind::kSelection:
      return AdapterKind::kSelection;
    case mojom::AdapterKind::kLayout:
      return AdapterKind::kLayout;
    case mojom::AdapterKind::kDocumentViewer:
    case mojom::AdapterKind::kMedia:
    case mojom::AdapterKind::kSite:
    case mojom::AdapterKind::kVision:
      // Adapters this endpoint does not have. A required one makes the whole
      // request UNSUPPORTED; an optional one is dropped and named.
      return std::nullopt;
  }
}

mojom::AdapterStatus ToMojom(AdapterStatus status) {
  switch (status) {
    case AdapterStatus::kOk:
      return mojom::AdapterStatus::kOk;
    case AdapterStatus::kIncomplete:
      return mojom::AdapterStatus::kIncomplete;
    case AdapterStatus::kConflicted:
      return mojom::AdapterStatus::kConflicted;
    case AdapterStatus::kUnsupported:
      return mojom::AdapterStatus::kUnsupported;
    case AdapterStatus::kFailed:
      return mojom::AdapterStatus::kFailed;
  }
}

mojom::Transformation ToMojom(Transformation transformation) {
  switch (transformation) {
    case Transformation::kNone:
      return mojom::Transformation::kNone;
    case Transformation::kNormalized:
      return mojom::Transformation::kNormalized;
    case Transformation::kInferred:
      return mojom::Transformation::kInferred;
    case Transformation::kRedacted:
      return mojom::Transformation::kRedacted;
  }
}

mojom::Sensitivity ToMojom(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kNotSensitive:
      return mojom::Sensitivity::kNotSensitive;
    case Sensitivity::kPersonal:
      return mojom::Sensitivity::kPersonal;
    case Sensitivity::kAccount:
      return mojom::Sensitivity::kAccount;
    case Sensitivity::kPayment:
      return mojom::Sensitivity::kPayment;
    case Sensitivity::kIdentity:
      return mojom::Sensitivity::kIdentity;
    case Sensitivity::kHealth:
      return mojom::Sensitivity::kHealth;
    case Sensitivity::kFinancial:
      return mojom::Sensitivity::kFinancial;
    case Sensitivity::kLegal:
      return mojom::Sensitivity::kLegal;
    case Sensitivity::kPrivateCommunication:
      return mojom::Sensitivity::kPrivateCommunication;
    case Sensitivity::kAdministration:
      return mojom::Sensitivity::kAdministration;
    case Sensitivity::kCredential:
      return mojom::Sensitivity::kCredential;
    case Sensitivity::kUnknownSensitive:
      return mojom::Sensitivity::kUnknownSensitive;
    case Sensitivity::kOneTimeCode:
      return mojom::Sensitivity::kOneTimeCode;
    case Sensitivity::kChallengeResponse:
      return mojom::Sensitivity::kChallengeResponse;
  }
}

std::optional<Sensitivity> FromMojom(mojom::Sensitivity sensitivity) {
  switch (sensitivity) {
    case mojom::Sensitivity::kNotSensitive:
      return Sensitivity::kNotSensitive;
    case mojom::Sensitivity::kPersonal:
      return Sensitivity::kPersonal;
    case mojom::Sensitivity::kAccount:
      return Sensitivity::kAccount;
    case mojom::Sensitivity::kPayment:
      return Sensitivity::kPayment;
    case mojom::Sensitivity::kIdentity:
      return Sensitivity::kIdentity;
    case mojom::Sensitivity::kHealth:
      return Sensitivity::kHealth;
    case mojom::Sensitivity::kFinancial:
      return Sensitivity::kFinancial;
    case mojom::Sensitivity::kLegal:
      return Sensitivity::kLegal;
    case mojom::Sensitivity::kPrivateCommunication:
      return Sensitivity::kPrivateCommunication;
    case mojom::Sensitivity::kAdministration:
      return Sensitivity::kAdministration;
    case mojom::Sensitivity::kCredential:
      return Sensitivity::kCredential;
    case mojom::Sensitivity::kUnknownSensitive:
      return Sensitivity::kUnknownSensitive;
    case mojom::Sensitivity::kOneTimeCode:
      return Sensitivity::kOneTimeCode;
    case mojom::Sensitivity::kChallengeResponse:
      return Sensitivity::kChallengeResponse;
  }
}

mojom::ChallengeKind ToMojom(ChallengeKind challenge) {
  switch (challenge) {
    case ChallengeKind::kNone:
      return mojom::ChallengeKind::kNone;
    case ChallengeKind::kImage:
      return mojom::ChallengeKind::kImage;
    case ChallengeKind::kInteractive:
      return mojom::ChallengeKind::kInteractive;
    case ChallengeKind::kOneTimeCode:
      return mojom::ChallengeKind::kOneTimeCode;
  }
}

std::optional<ChallengeKind> FromMojom(mojom::ChallengeKind challenge) {
  switch (challenge) {
    case mojom::ChallengeKind::kNone:
      return ChallengeKind::kNone;
    case mojom::ChallengeKind::kImage:
      return ChallengeKind::kImage;
    case mojom::ChallengeKind::kInteractive:
      return ChallengeKind::kInteractive;
    case mojom::ChallengeKind::kOneTimeCode:
      return ChallengeKind::kOneTimeCode;
  }
}

mojom::WarningCode ToMojom(WarningCode code) {
  switch (code) {
    case WarningCode::kAdapterUnavailable:
      return mojom::WarningCode::kAdapterUnavailable;
    case WarningCode::kAdapterFailed:
      return mojom::WarningCode::kAdapterFailed;
    case WarningCode::kConflictingEvidence:
      return mojom::WarningCode::kConflictingEvidence;
    case WarningCode::kCrossOriginFrameOmitted:
      return mojom::WarningCode::kCrossOriginFrameOmitted;
    case WarningCode::kClosedShadowRootNotProjected:
      return mojom::WarningCode::kClosedShadowRootNotProjected;
    case WarningCode::kVirtualizedContentPartial:
      return mojom::WarningCode::kVirtualizedContentPartial;
    case WarningCode::kCanvasWithoutSemantics:
      return mojom::WarningCode::kCanvasWithoutSemantics;
    case WarningCode::kSensitiveZoneSuppressed:
      return mojom::WarningCode::kSensitiveZoneSuppressed;
    case WarningCode::kUrlMinimizedByPolicy:
      return mojom::WarningCode::kUrlMinimizedByPolicy;
    case WarningCode::kDeadlineReached:
      return mojom::WarningCode::kDeadlineReached;
    case WarningCode::kMemoryPressure:
      return mojom::WarningCode::kMemoryPressure;
    case WarningCode::kInjectionSignalDetected:
      return mojom::WarningCode::kInjectionSignalDetected;
  }
}

mojom::BudgetKind ToMojom(BudgetKind kind) {
  switch (kind) {
    case BudgetKind::kNodes:
      return mojom::BudgetKind::kMaxNodes;
    case BudgetKind::kTextBytes:
      return mojom::BudgetKind::kMaxTextBytes;
    case BudgetKind::kTotalBytes:
      return mojom::BudgetKind::kMaxTotalBytes;
    case BudgetKind::kDepth:
      return mojom::BudgetKind::kMaxDepth;
    case BudgetKind::kDeadline:
      return mojom::BudgetKind::kDeadline;
    case BudgetKind::kNone:
      // Never emitted: the caller only asks when truncation.truncated is set,
      // and MarkTruncated() always records a kind alongside it.
      //
      // VERIFY AT SP-04: that base::NotReached() is [[noreturn]] at the pin.
      // If it is not, this switch needs a trailing return and every other
      // NOTREACHED() in this component needs the same treatment.
      NOTREACHED();
  }
}

mojom::ObservationScope ToMojom(ExtractionScope scope) {
  switch (scope) {
    case ExtractionScope::kViewport:
      return mojom::ObservationScope::kViewport;
    case ExtractionScope::kInteractive:
      return mojom::ObservationScope::kInteractive;
    case ExtractionScope::kSelection:
      return mojom::ObservationScope::kSelection;
    case ExtractionScope::kSection:
      return mojom::ObservationScope::kSection;
    case ExtractionScope::kDocument:
      return mojom::ObservationScope::kDocument;
  }
}

ExtractionScope FromMojom(mojom::ObservationScope scope) {
  switch (scope) {
    case mojom::ObservationScope::kViewport:
      return ExtractionScope::kViewport;
    case mojom::ObservationScope::kInteractive:
      return ExtractionScope::kInteractive;
    case mojom::ObservationScope::kSelection:
      return ExtractionScope::kSelection;
    case mojom::ObservationScope::kSection:
      return ExtractionScope::kSection;
    case mojom::ObservationScope::kDocument:
      return ExtractionScope::kDocument;
  }
}

mojom::DocumentLifecycleState ToMojom(DocumentLifecycle lifecycle) {
  switch (lifecycle) {
    case DocumentLifecycle::kActive:
      return mojom::DocumentLifecycleState::kActive;
    case DocumentLifecycle::kSpeculative:
      return mojom::DocumentLifecycleState::kSpeculative;
    case DocumentLifecycle::kPendingCommit:
      return mojom::DocumentLifecycleState::kPendingCommit;
    case DocumentLifecycle::kPrerendering:
      return mojom::DocumentLifecycleState::kPrerendering;
    case DocumentLifecycle::kFrozen:
      return mojom::DocumentLifecycleState::kFrozen;
    case DocumentLifecycle::kBackForwardCached:
      return mojom::DocumentLifecycleState::kBackForwardCached;
    case DocumentLifecycle::kCrashed:
      return mojom::DocumentLifecycleState::kCrashed;
    case DocumentLifecycle::kDestroyed:
      return mojom::DocumentLifecycleState::kDestroyed;
  }
}

DocumentLifecycle FromMojom(mojom::DocumentLifecycleState state) {
  switch (state) {
    case mojom::DocumentLifecycleState::kActive:
      return DocumentLifecycle::kActive;
    case mojom::DocumentLifecycleState::kSpeculative:
      return DocumentLifecycle::kSpeculative;
    case mojom::DocumentLifecycleState::kPendingCommit:
      return DocumentLifecycle::kPendingCommit;
    case mojom::DocumentLifecycleState::kPrerendering:
      return DocumentLifecycle::kPrerendering;
    case mojom::DocumentLifecycleState::kFrozen:
      return DocumentLifecycle::kFrozen;
    case mojom::DocumentLifecycleState::kBackForwardCached:
      return DocumentLifecycle::kBackForwardCached;
    case mojom::DocumentLifecycleState::kCrashed:
      return DocumentLifecycle::kCrashed;
    case mojom::DocumentLifecycleState::kDestroyed:
      return DocumentLifecycle::kDestroyed;
  }
}

}  // namespace taffy::wire
