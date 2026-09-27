// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_inspector_projection_mapper.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "taffy/common/public/bip_observation.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_api::mojom;

mojom::PageInspectorNodeRole ToNodeRole(InspectorNodeRole role) {
  return static_cast<mojom::PageInspectorNodeRole>(role);
}

mojom::PageInspectorSensitivity ToSensitivity(
    InspectorSensitivity sensitivity) {
  return static_cast<mojom::PageInspectorSensitivity>(sensitivity);
}

mojom::PageInspectorRelationship ToRelationship(
    InspectorRelationship relationship) {
  return static_cast<mojom::PageInspectorRelationship>(relationship);
}

std::optional<mojom::PageInspectorAdapterKind> ToAdapterKind(
    AdapterKind adapter) {
  switch (adapter) {
    case AdapterKind::kDom:
      return mojom::PageInspectorAdapterKind::kStructure;
    case AdapterKind::kAccessibility:
      return mojom::PageInspectorAdapterKind::kAccessibility;
    case AdapterKind::kForms:
      return mojom::PageInspectorAdapterKind::kForms;
    case AdapterKind::kMetadata:
      return mojom::PageInspectorAdapterKind::kMetadata;
    case AdapterKind::kBrowser:
    case AdapterKind::kSite:
      return mojom::PageInspectorAdapterKind::kBrowser;
    case AdapterKind::kDocumentViewer:
    case AdapterKind::kMedia:
    case AdapterKind::kVision:
    case AdapterKind::kSelection:
    case AdapterKind::kLayout:
      return std::nullopt;
  }
  return std::nullopt;
}

mojom::PageInspectorAdapterStatus ToAdapterStatus(AdapterStatus status) {
  switch (status) {
    case AdapterStatus::kOk:
      return mojom::PageInspectorAdapterStatus::kComplete;
    case AdapterStatus::kIncomplete:
      return mojom::PageInspectorAdapterStatus::kPartial;
    case AdapterStatus::kConflicted:
      return mojom::PageInspectorAdapterStatus::kConflict;
    case AdapterStatus::kUnsupported:
      return mojom::PageInspectorAdapterStatus::kUnsupported;
    case AdapterStatus::kFailed:
      return mojom::PageInspectorAdapterStatus::kFailed;
  }
  return mojom::PageInspectorAdapterStatus::kFailed;
}

mojom::PageInspectorBudgetKind ToBudget(BudgetKind budget) {
  return static_cast<mojom::PageInspectorBudgetKind>(budget);
}

std::optional<mojom::PageInspectorWarningCode> ToWarning(uint8_t warning) {
  switch (warning) {
    case 0:
      return mojom::PageInspectorWarningCode::kSourceUnavailable;
    case 1:
      return mojom::PageInspectorWarningCode::kSourceFailed;
    case 2:
      return mojom::PageInspectorWarningCode::kConflict;
    case 3:
      return mojom::PageInspectorWarningCode::kFrameOmitted;
    case 4:
    case 5:
      return mojom::PageInspectorWarningCode::kContentPartial;
    case 6:
      return mojom::PageInspectorWarningCode::kSemanticsMissing;
    case 7:
      return mojom::PageInspectorWarningCode::kContentWithheld;
    case 8:
      return mojom::PageInspectorWarningCode::kLocationMinimized;
    case 9:
      return mojom::PageInspectorWarningCode::kDeadline;
    case 10:
      return mojom::PageInspectorWarningCode::kResourcePressure;
    case 11:
      return mojom::PageInspectorWarningCode::kSuspiciousContent;
    default:
      return std::nullopt;
  }
}

mojom::PageInspectorDocumentState ToDocumentState(
    DocumentLifecycleState state) {
  switch (state) {
    case DocumentLifecycleState::kActive:
      return mojom::PageInspectorDocumentState::kActive;
    case DocumentLifecycleState::kFrozen:
    case DocumentLifecycleState::kBackForwardCached:
      return mojom::PageInspectorDocumentState::kFrozen;
    case DocumentLifecycleState::kSpeculative:
    case DocumentLifecycleState::kPendingCommit:
    case DocumentLifecycleState::kPrerendering:
    case DocumentLifecycleState::kCrashed:
    case DocumentLifecycleState::kDestroyed:
      return mojom::PageInspectorDocumentState::kUnavailable;
  }
  return mojom::PageInspectorDocumentState::kUnavailable;
}

}  // namespace

core_api::mojom::PageInspectorSnapshotViewPtr ProjectPageInspectorObservation(
    const ObservationEnvelope& observation) {
  // The same three codes admitted everywhere else a reading is read: the ones
  // the renderer attaches a snapshot to. This surface is built to show a
  // disagreement — `PageInspectorAdapterStatus::kConflict` exists for exactly
  // that and is set per adapter by `ToAdapterStatus` — so refusing the whole
  // projection for one was the one thing that guaranteed nobody could see
  // which adapter had disagreed (decision 0207).
  if ((observation.code != ObservationResultCode::kOk &&
       observation.code != ObservationResultCode::kIncomplete &&
       observation.code != ObservationResultCode::kConflicted) ||
      !observation.inspector_projection || observation.origin.is_opaque() ||
      observation.inspector_projection->nodes.size() >
          mojom::kMaxPageInspectorNodes ||
      observation.inspector_projection->edges.size() >
          mojom::kMaxPageInspectorEdges ||
      observation.adapters.size() > mojom::kMaxPageInspectorAdapters ||
      observation.frames.size() != 1u ||
      observation.warning_codes.size() > mojom::kMaxPageInspectorWarnings ||
      observation.truncation.budgets_reached.size() >
          mojom::kMaxPageInspectorBudgets) {
    return nullptr;
  }
  const GURL origin(observation.origin.serialization);
  const std::string host(origin.host());
  if (!origin.is_valid() || host.empty() ||
      host.size() > mojom::kMaxPageInspectorHostBytes) {
    return nullptr;
  }

  auto output = mojom::PageInspectorSnapshotView::New();
  // The projection mapper has no catalogue access. The manager replaces this
  // conservative terminal only after the exact raw observation has been
  // matched in the isolated Rust service.
  output->site_skill_offer_availability =
      mojom::SiteSkillOfferAvailability::kCoreUnavailable;
  output->document_id = "selected-page";
  output->document_revision = observation.graph_revision;
  output->host = host;
  output->secure_context = observation.is_potentially_trustworthy;
  output->private_profile = observation.is_incognito;
  output->document_state = ToDocumentState(observation.lifecycle_state);

  for (const AdapterReport& adapter : observation.adapters) {
    const std::optional<mojom::PageInspectorAdapterKind> kind =
        ToAdapterKind(adapter.adapter);
    if (!kind) {
      continue;
    }
    auto projected = mojom::PageInspectorAdapterView::New();
    projected->kind = *kind;
    projected->status = ToAdapterStatus(adapter.status);
    projected->version = adapter.adapter_version;
    output->adapters.push_back(std::move(projected));
  }

  for (const InspectorNodeProjection& node :
       observation.inspector_projection->nodes) {
    if (node.display_id.empty() ||
        node.display_id.size() > mojom::kMaxPageInspectorIdentifierBytes ||
        (node.name && node.name->size() > mojom::kMaxPageInspectorNameBytes)) {
      return nullptr;
    }
    auto projected = mojom::PageInspectorNodeView::New();
    projected->display_id = node.display_id;
    projected->role = ToNodeRole(node.role);
    projected->name = node.name;
    projected->sensitivity = ToSensitivity(node.sensitivity);
    projected->text_run_count = node.text_run_count;
    projected->text_byte_count = node.text_byte_count;
    projected->value_present = node.value_present;
    projected->value_withheld = node.value_withheld;
    output->nodes.push_back(std::move(projected));
  }
  for (const InspectorEdgeProjection& edge :
       observation.inspector_projection->edges) {
    if (edge.from_display_id.empty() || edge.to_display_id.empty() ||
        edge.from_display_id.size() >
            mojom::kMaxPageInspectorIdentifierBytes ||
        edge.to_display_id.size() >
            mojom::kMaxPageInspectorIdentifierBytes) {
      return nullptr;
    }
    auto projected = mojom::PageInspectorEdgeView::New();
    projected->from_display_id = edge.from_display_id;
    projected->to_display_id = edge.to_display_id;
    projected->relationship = ToRelationship(edge.relationship);
    projected->inferred = edge.inferred;
    output->edges.push_back(std::move(projected));
  }

  const auto main_frame = std::find_if(
      observation.frames.begin(), observation.frames.end(),
      [](const FrameSummary& frame) { return frame.is_main_frame; });
  if (main_frame == observation.frames.end() || !main_frame->included) {
    return nullptr;
  }
  auto frame = mojom::PageInspectorFrameView::New();
  frame->main_frame = true;
  frame->out_of_process = main_frame->is_out_of_process;
  frame->cross_origin = main_frame->is_cross_origin_to_parent;
  frame->included = main_frame->included;
  output->frames.push_back(std::move(frame));

  output->truncation = mojom::PageInspectorTruncationView::New();
  output->truncation->truncated = observation.truncation.truncated;
  for (BudgetKind budget : observation.truncation.budgets_reached) {
    output->truncation->budgets_reached.push_back(ToBudget(budget));
  }
  output->truncation->omitted_node_count =
      observation.truncation.omitted_node_count;
  output->truncation->omitted_text_bytes =
      observation.truncation.omitted_text_bytes;
  output->truncation->omitted_frame_count =
      observation.truncation.omitted_frame_count;
  output->truncation->may_change_answer =
      observation.truncation.may_change_answer;

  output->redaction = mojom::PageInspectorRedactionView::New();
  output->redaction->redacted_field_count =
      observation.redaction.redacted_field_count;
  output->redaction->suppressed_secret_count =
      observation.redaction.suppressed_secret_value_count;
  output->redaction->sensitive_zone_count =
      observation.redaction.sensitive_zone_count;
  output->redaction->filtered_frame_count =
      observation.redaction.policy_filtered_frame_count;

  for (uint8_t warning : observation.warning_codes) {
    const std::optional<mojom::PageInspectorWarningCode> projected =
        ToWarning(warning);
    if (projected &&
        std::find(output->warnings.begin(), output->warnings.end(),
                  *projected) == output->warnings.end()) {
      output->warnings.push_back(*projected);
    }
  }
  return output;
}

}  // namespace taffy
