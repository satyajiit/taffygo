// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Negotiation and stream types: what an endpoint says it supports, and what a
// subscription is allowed to ask for.
//
// Every function here returns std::optional in the mojom-to-public direction
// and nullopt for a value this build does not know. That is protocol section
// 6.2's rule made mechanical: an unknown enum value is unsupported, never the
// least restrictive known value. Dropping the value is what "unsupported"
// looks like in a list; coercing it to a neighbour is what a compromised or
// merely newer endpoint would want instead.
//
// Declared alongside the action and node conversions in
// bip_mojom_conversions.h: the overload sets are one vocabulary to a caller,
// and splitting the header would make every caller include both.

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"

#include <utility>

#include "taffy/components/intelligence/content/bip_schema_version.h"

namespace taffy {

std::optional<AdapterKind> FromMojom(mojom::AdapterKind adapter) {
  switch (adapter) {
    case mojom::AdapterKind::kDom:
      return AdapterKind::kDom;
    case mojom::AdapterKind::kAccessibility:
      return AdapterKind::kAccessibility;
    case mojom::AdapterKind::kForms:
      return AdapterKind::kForms;
    case mojom::AdapterKind::kMetadata:
      return AdapterKind::kMetadata;
    case mojom::AdapterKind::kBrowser:
      return AdapterKind::kBrowser;
    case mojom::AdapterKind::kDocumentViewer:
      return AdapterKind::kDocumentViewer;
    case mojom::AdapterKind::kMedia:
      return AdapterKind::kMedia;
    case mojom::AdapterKind::kSite:
      return AdapterKind::kSite;
    case mojom::AdapterKind::kVision:
      return AdapterKind::kVision;
    case mojom::AdapterKind::kSelection:
      return AdapterKind::kSelection;
    case mojom::AdapterKind::kLayout:
      return AdapterKind::kLayout;
  }
  return std::nullopt;
}

mojom::AdapterKind ToMojom(AdapterKind adapter) {
  switch (adapter) {
    case AdapterKind::kDom:
      return mojom::AdapterKind::kDom;
    case AdapterKind::kAccessibility:
      return mojom::AdapterKind::kAccessibility;
    case AdapterKind::kForms:
      return mojom::AdapterKind::kForms;
    case AdapterKind::kMetadata:
      return mojom::AdapterKind::kMetadata;
    case AdapterKind::kBrowser:
      return mojom::AdapterKind::kBrowser;
    case AdapterKind::kDocumentViewer:
      return mojom::AdapterKind::kDocumentViewer;
    case AdapterKind::kMedia:
      return mojom::AdapterKind::kMedia;
    case AdapterKind::kSite:
      return mojom::AdapterKind::kSite;
    case AdapterKind::kVision:
      return mojom::AdapterKind::kVision;
    case AdapterKind::kSelection:
      return mojom::AdapterKind::kSelection;
    case AdapterKind::kLayout:
      return mojom::AdapterKind::kLayout;
  }
  return mojom::AdapterKind::kDom;
}

std::optional<ObservationScope> FromMojom(mojom::ObservationScope scope) {
  switch (scope) {
    case mojom::ObservationScope::kViewport:
      return ObservationScope::kViewport;
    case mojom::ObservationScope::kInteractive:
      return ObservationScope::kInteractive;
    case mojom::ObservationScope::kSelection:
      return ObservationScope::kSelection;
    case mojom::ObservationScope::kSection:
      return ObservationScope::kSection;
    case mojom::ObservationScope::kDocument:
      return ObservationScope::kDocument;
  }
  return std::nullopt;
}

mojom::ObservationScope ToMojom(ObservationScope scope) {
  switch (scope) {
    case ObservationScope::kViewport:
      return mojom::ObservationScope::kViewport;
    case ObservationScope::kInteractive:
      return mojom::ObservationScope::kInteractive;
    case ObservationScope::kSelection:
      return mojom::ObservationScope::kSelection;
    case ObservationScope::kSection:
      return mojom::ObservationScope::kSection;
    case ObservationScope::kDocument:
      return mojom::ObservationScope::kDocument;
  }
  // Unreachable for a valid value. The narrowest scope is the safe answer:
  // an unrecognized scope must never be sent as "the whole document".
  return mojom::ObservationScope::kSelection;
}

std::optional<BudgetKind> FromMojom(mojom::BudgetKind budget) {
  switch (budget) {
    case mojom::BudgetKind::kMaxNodes:
      return BudgetKind::kMaxNodes;
    case mojom::BudgetKind::kMaxTextBytes:
      return BudgetKind::kMaxTextBytes;
    case mojom::BudgetKind::kMaxTotalBytes:
      return BudgetKind::kMaxTotalBytes;
    case mojom::BudgetKind::kMaxDepth:
      return BudgetKind::kMaxDepth;
    case mojom::BudgetKind::kMaxFrames:
      return BudgetKind::kMaxFrames;
    case mojom::BudgetKind::kMaxMessageBytes:
      return BudgetKind::kMaxMessageBytes;
    case mojom::BudgetKind::kDeadline:
      return BudgetKind::kDeadline;
  }
  return std::nullopt;
}

std::optional<DeltaClass> FromMojom(mojom::DeltaCategory category) {
  switch (category) {
    case mojom::DeltaCategory::kText:
      return DeltaClass::kText;
    case mojom::DeltaCategory::kLayout:
      return DeltaClass::kLayout;
    case mojom::DeltaCategory::kAttribute:
      return DeltaClass::kAttribute;
    case mojom::DeltaCategory::kEdge:
      return DeltaClass::kEdge;
    case mojom::DeltaCategory::kNodeAdded:
      return DeltaClass::kNodeAdded;
    case mojom::DeltaCategory::kNodeRemoved:
      return DeltaClass::kNodeRemoved;
    case mojom::DeltaCategory::kLifecycle:
      return DeltaClass::kLifecycle;
  }
  return std::nullopt;
}

std::optional<BackpressureAction> FromMojom(mojom::BackpressureAction action) {
  switch (action) {
    case mojom::BackpressureAction::kCoalesced:
      return BackpressureAction::kCoalesced;
    case mojom::BackpressureAction::kScopeReduced:
      return BackpressureAction::kScopeReduced;
    case mojom::BackpressureAction::kSubscriptionPaused:
      return BackpressureAction::kSubscriptionPaused;
    case mojom::BackpressureAction::kResnapshotRequested:
      return BackpressureAction::kResnapshotRequested;
    case mojom::BackpressureAction::kSubscriptionStopped:
      return BackpressureAction::kSubscriptionStopped;
  }
  return std::nullopt;
}

std::optional<InvalidationCode> FromMojom(mojom::InvalidationReason reason) {
  switch (reason) {
    case mojom::InvalidationReason::kCrossDocumentCommit:
      return InvalidationCode::kCrossDocumentCommit;
    case mojom::InvalidationReason::kHistoryRouteChange:
      return InvalidationCode::kHistoryRouteChange;
    case mojom::InvalidationReason::kChildFrameNavigation:
      return InvalidationCode::kChildFrameNavigation;
    case mojom::InvalidationReason::kOriginChanged:
      return InvalidationCode::kOriginChanged;
    case mojom::InvalidationReason::kPrerenderActivation:
      return InvalidationCode::kPrerenderActivation;
    case mojom::InvalidationReason::kBfcacheEntered:
      return InvalidationCode::kBfcacheEntered;
    case mojom::InvalidationReason::kBfcacheRestored:
      return InvalidationCode::kBfcacheRestored;
    case mojom::InvalidationReason::kSequenceGap:
      return InvalidationCode::kSequenceGap;
    case mojom::InvalidationReason::kDeltaOverflow:
      return InvalidationCode::kDeltaOverflow;
    case mojom::InvalidationReason::kUnknownDeltaField:
      return InvalidationCode::kUnknownDeltaField;
    case mojom::InvalidationReason::kAdapterRestart:
      return InvalidationCode::kAdapterRestart;
    case mojom::InvalidationReason::kBrokerInvalidation:
      return InvalidationCode::kBrokerInvalidation;
    case mojom::InvalidationReason::kMemoryPressure:
      return InvalidationCode::kMemoryPressure;
    case mojom::InvalidationReason::kRendererCrashed:
      return InvalidationCode::kRendererCrashed;
    case mojom::InvalidationReason::kEndpointDisconnected:
      return InvalidationCode::kEndpointDisconnected;
    case mojom::InvalidationReason::kTabClosed:
      return InvalidationCode::kTabClosed;
    case mojom::InvalidationReason::kProfileTeardown:
      return InvalidationCode::kProfileTeardown;
    case mojom::InvalidationReason::kFrameDetached:
      return InvalidationCode::kFrameDetached;
  }
  return std::nullopt;
}

mojom::InvalidationReason ToMojom(InvalidationCode code) {
  switch (code) {
    case InvalidationCode::kCrossDocumentCommit:
      return mojom::InvalidationReason::kCrossDocumentCommit;
    case InvalidationCode::kHistoryRouteChange:
      return mojom::InvalidationReason::kHistoryRouteChange;
    case InvalidationCode::kChildFrameNavigation:
      return mojom::InvalidationReason::kChildFrameNavigation;
    case InvalidationCode::kOriginChanged:
      return mojom::InvalidationReason::kOriginChanged;
    case InvalidationCode::kPrerenderActivation:
      return mojom::InvalidationReason::kPrerenderActivation;
    case InvalidationCode::kBfcacheEntered:
      return mojom::InvalidationReason::kBfcacheEntered;
    case InvalidationCode::kBfcacheRestored:
      return mojom::InvalidationReason::kBfcacheRestored;
    case InvalidationCode::kSequenceGap:
      return mojom::InvalidationReason::kSequenceGap;
    case InvalidationCode::kDeltaOverflow:
      return mojom::InvalidationReason::kDeltaOverflow;
    case InvalidationCode::kUnknownDeltaField:
      return mojom::InvalidationReason::kUnknownDeltaField;
    case InvalidationCode::kAdapterRestart:
      return mojom::InvalidationReason::kAdapterRestart;
    case InvalidationCode::kBrokerInvalidation:
      return mojom::InvalidationReason::kBrokerInvalidation;
    case InvalidationCode::kMemoryPressure:
      return mojom::InvalidationReason::kMemoryPressure;
    case InvalidationCode::kRendererCrashed:
      return mojom::InvalidationReason::kRendererCrashed;
    case InvalidationCode::kEndpointDisconnected:
      return mojom::InvalidationReason::kEndpointDisconnected;
    case InvalidationCode::kTabClosed:
      return mojom::InvalidationReason::kTabClosed;
    case InvalidationCode::kProfileTeardown:
      return mojom::InvalidationReason::kProfileTeardown;
    case InvalidationCode::kFrameDetached:
      return mojom::InvalidationReason::kFrameDetached;
  }
  return mojom::InvalidationReason::kBrokerInvalidation;
}

std::optional<DocumentLifecycleState> FromMojom(
    mojom::DocumentLifecycleState state) {
  switch (state) {
    case mojom::DocumentLifecycleState::kActive:
      return DocumentLifecycleState::kActive;
    case mojom::DocumentLifecycleState::kSpeculative:
      return DocumentLifecycleState::kSpeculative;
    case mojom::DocumentLifecycleState::kPendingCommit:
      return DocumentLifecycleState::kPendingCommit;
    case mojom::DocumentLifecycleState::kPrerendering:
      return DocumentLifecycleState::kPrerendering;
    case mojom::DocumentLifecycleState::kFrozen:
      return DocumentLifecycleState::kFrozen;
    case mojom::DocumentLifecycleState::kBackForwardCached:
      return DocumentLifecycleState::kBackForwardCached;
    case mojom::DocumentLifecycleState::kCrashed:
      return DocumentLifecycleState::kCrashed;
    case mojom::DocumentLifecycleState::kDestroyed:
      return DocumentLifecycleState::kDestroyed;
  }
  return std::nullopt;
}

ProcessBudgetLimits FromMojom(const mojom::ProtocolLimits& limits) {
  ProcessBudgetLimits out;
  out.max_message_bytes = limits.max_message_bytes;
  out.max_nodes = limits.max_nodes;
  out.max_text_bytes = limits.max_text_bytes;
  out.max_total_bytes = limits.max_total_bytes;
  out.max_depth = limits.max_depth;
  out.max_frames = limits.max_frames;
  out.max_delta_queue_depth = limits.max_delta_queue_depth;
  out.max_snapshot_deadline_ms = limits.max_snapshot_deadline_ms;
  out.min_delta_interval_ms = limits.min_delta_interval_ms;
  return out;
}

TruncationSummary FromMojom(const mojom::Truncation& truncation) {
  TruncationSummary out;
  out.truncated = truncation.truncated;
  out.omitted_node_count = truncation.omitted_node_count;
  out.omitted_text_bytes = truncation.omitted_text_bytes;
  out.omitted_frame_count = truncation.omitted_frame_count;
  out.may_change_answer = truncation.may_change_answer;
  for (mojom::BudgetKind budget : truncation.budgets_reached) {
    if (std::optional<BudgetKind> mapped = FromMojom(budget)) {
      out.budgets_reached.push_back(*mapped);
    }
  }
  return out;
}

mojom::PageSubscriptionOptionsPtr ToMojom(const SubscriptionRequest& clamped) {
  auto options = mojom::PageSubscriptionOptions::New();
  options->schema_version = kBipSchemaVersion;
  options->request_id = clamped.request_id.value;
  options->frame_id = clamped.frame_id.value;
  options->expected_page_epoch = clamped.expected_page_epoch.value;
  options->scope = ToMojom(clamped.scope);
  for (const AdapterRequirement& adapter : clamped.adapters) {
    auto requirement = mojom::AdapterRequirement::New();
    requirement->adapter = ToMojom(adapter.adapter);
    requirement->requirement =
        static_cast<mojom::AdapterRequirementLevel>(adapter.requirement);
    options->adapters.push_back(std::move(requirement));
  }
  for (SemanticField field : clamped.requested_fields) {
    options->requested_fields.push_back(static_cast<mojom::SemanticField>(field));
  }
  // The renderer is told the byte and node ceilings it must respect, not the
  // queue depth: the queue is this process's, and a renderer that knew its
  // depth could time a burst to fill it.
  options->max_nodes = 0;
  options->max_total_bytes = clamped.budget.max_delta_bytes;
  options->coalescing_window_ms = clamped.budget.coalescing_window_ms;
  options->include_text_deltas = clamped.include_text_deltas;
  options->include_layout_deltas = clamped.include_layout_deltas;
  return options;
}

}  // namespace taffy
