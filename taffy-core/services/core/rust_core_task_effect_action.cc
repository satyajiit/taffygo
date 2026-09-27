// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One task action effect, projected. It is the largest of the three effect
// projections by some distance — an action carries the capability, the scope,
// the approval and the destination all at once — and it lives here so that the
// binding which assembles all three stays readable beside them.

#include <stdint.h>

#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_records.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

std::optional<mojom::TaskExecutableActionPtr> FormApprovalAction(
    const bridge::BridgeTaskEffect& input) {
  const auto action_class =
      ClosedEnum(input.action_class, mojom::PolicyActionClass::kProfileStoreRead);
  const auto operation = ClosedEnum(
      input.action_operation, mojom::TaskActionOperationKind::kOpenTabsList);
  if (!action_class || !operation || !ValidIdentifier(input.tool_name) ||
      !ValidIdentifier(input.tab_id) || !input.has_node_id ||
      !ValidIdentifier(input.node_id) || input.canonical_intent.empty() ||
      input.canonical_intent.size() > mojom::kMaxCanonicalActionIntentBytes ||
      input.has_destination_origin || !input.destination_origin.empty() ||
      input.has_destination_address || !input.destination_address.empty() ||
      input.has_operand_handle || !input.operand_handle.empty() ||
      input.has_transient_search_query || !input.transient_search_query.empty() ||
      input.has_task_tab_binding || input.has_task_tab_target ||
      !input.task_tab_browser_session_id.empty() ||
      !input.task_tab_target_tab_id.empty() ||
      !input.task_tab_target_frame_id.empty() ||
      !input.task_tab_target_page_epoch.empty() ||
      input.task_tab_target_graph_revision != 0u ||
      input.has_task_download_binding ||
      !input.task_download_browser_session_id.empty() ||
      !input.task_download_id.empty() || input.has_task_store_binding ||
      input.task_store_has_query || !input.task_store_query.empty() ||
      input.task_store_limit != 0u) {
    return std::nullopt;
  }

  const bool operation_matches =
      (*operation == mojom::TaskActionOperationKind::kFormFill &&
       *action_class == mojom::PolicyActionClass::kFillField &&
       input.tool_name == "browser.form.fill") ||
      (*operation == mojom::TaskActionOperationKind::kFormSelect &&
       *action_class == mojom::PolicyActionClass::kSelectOption &&
       input.tool_name == "browser.form.select") ||
      (*operation == mojom::TaskActionOperationKind::kFormToggle &&
       *action_class == mojom::PolicyActionClass::kToggleControl &&
       input.tool_name == "browser.form.toggle") ||
      (*operation == mojom::TaskActionOperationKind::kFormSubmit &&
       *action_class == mojom::PolicyActionClass::kSubmitForm &&
       input.tool_name == "browser.form.submit");
  if (!operation_matches) {
    return std::nullopt;
  }

  std::optional<mojom::TaskActionInputPtr> action_input =
      ActionInput(input, *operation);
  if (!action_input) {
    return std::nullopt;
  }
  auto out = mojom::TaskExecutableAction::New();
  out->action_class = *action_class;
  out->operation_kind = *operation;
  out->canonical_intent.assign(input.canonical_intent.begin(),
                               input.canonical_intent.end());
  out->tool_name = std::string(input.tool_name);
  out->tab_id = std::string(input.tab_id);
  out->node_id = std::string(input.node_id);
  out->input = std::move(*action_input);
  return std::optional<mojom::TaskExecutableActionPtr>(std::move(out));
}

std::optional<mojom::TaskActionEffectPtr> ActionEffect(
    const bridge::BridgeTaskEffect& input) {
  const auto action_class =
      ClosedEnum(input.action_class, mojom::PolicyActionClass::kProfileStoreRead);
  const auto action_operation = ClosedEnum(
      input.action_operation, mojom::TaskActionOperationKind::kOpenTabsList);
  const auto postcondition = ClosedEnum(
      input.postcondition, mojom::TaskActionPostcondition::kStoreRowsListed);
  if (!action_class || !action_operation || !postcondition ||
      !ValidIdentifier(input.action_id) ||
      !ValidIdentifier(input.capability_id) ||
      !ValidIdentifier(input.dispatch_id) || !ValidIdentifier(input.frame_id) ||
      !ValidIdentifier(input.page_epoch) ||
      input.has_opaque_origin_id != !input.opaque_origin_id.empty() ||
      (input.has_opaque_origin_id == !input.normalized_origin.empty()) ||
      (input.has_opaque_origin_id
           ? !ValidIdentifier(input.opaque_origin_id)
           : !ValidIdentifier(input.normalized_origin)) ||
      !ValidIdentifier(input.tool_name) || !ValidIdentifier(input.tab_id) ||
      !ValidIdentifier(input.action_idempotency_key) ||
      !ValidDigest(input.proposal_digest) || input.canonical_intent.empty() ||
      input.has_node_id != !input.node_id.empty() ||
      input.has_destination_origin != !input.destination_origin.empty() ||
      input.has_destination_address != !input.destination_address.empty() ||
      input.has_operand_handle != !input.operand_handle.empty() ||
      input.has_operand_handle !=
          (*action_operation == mojom::TaskActionOperationKind::kSearch ||
           *action_operation ==
               mojom::TaskActionOperationKind::kHistorySearch ||
           *action_operation ==
               mojom::TaskActionOperationKind::kBookmarksSearch) ||
      input.has_transient_search_query !=
          !input.transient_search_query.empty() ||
      input.has_transient_search_query !=
          (*action_operation == mojom::TaskActionOperationKind::kSearch) ||
      input.transient_search_query.size() >
          mojom::kMaxTransientSearchQueryBytes ||
      input.canonical_intent.size() > mojom::kMaxCanonicalActionIntentBytes ||
      // A revision floor is required exactly where a node handle exists to pin
      // it to (decision 0051 section 6). An action naming no node — a
      // navigation is the standing case — has nothing to be stale against, and
      // may be the first thing a task does, before any observation has
      // produced a revision at all.
      //
      // The browser process reaches this rule through
      // `GraphRevisionFloorIsWellFormed` in taffy/common/public/bip_identity.h.
      // This file cannot: the core-service boundary's DEPS admits the contract
      // and nothing else, deliberately. Two terms restated behind a named
      // boundary is the cost of that boundary; a shared header would not be.
      (input.has_node_id && input.graph_revision == 0u) ||
      input.preconditions.empty() ||
      input.preconditions.size() > mojom::kMaxTaskActionPreconditions) {
    return std::nullopt;
  }
  // Library tools execute inside the core through RunLibraryTool. They may
  // never be smuggled into the renderer-facing DispatchAction body.
  if (*action_class == mojom::PolicyActionClass::kLibraryRead ||
      *action_class == mojom::PolicyActionClass::kLibraryWrite ||
      *action_operation == mojom::TaskActionOperationKind::kLibrarySearch ||
      *action_operation == mojom::TaskActionOperationKind::kLibrarySave ||
      *action_operation == mojom::TaskActionOperationKind::kLibraryRemove) {
    return std::nullopt;
  }
  const bool lists_tabs =
      *action_operation == mojom::TaskActionOperationKind::kTabsList;
  const bool activates_tab =
      *action_operation == mojom::TaskActionOperationKind::kTabsActivate;
  const bool closes_tab =
      *action_operation == mojom::TaskActionOperationKind::kTabsClose;
  const bool task_tab_operation = lists_tabs || activates_tab || closes_tab;
  const bool starts_download =
      *action_operation == mojom::TaskActionOperationKind::kDownloadStart;
  const bool lists_downloads =
      *action_operation == mojom::TaskActionOperationKind::kDownloadList;
  const bool cancels_download =
      *action_operation == mojom::TaskActionOperationKind::kDownloadCancel;
  const bool task_download_operation =
      starts_download || lists_downloads || cancels_download;
  // A store read (decision 0133) names its store and kind by operation; the
  // binding carries the row cap and, for the two search kinds, the resolved
  // words. The class is exactly ProfileStoreRead and the postcondition is
  // exactly the rows listed, in both directions.
  const bool searches_store =
      *action_operation == mojom::TaskActionOperationKind::kHistorySearch ||
      *action_operation == mojom::TaskActionOperationKind::kBookmarksSearch;
  const bool task_store_operation =
      searches_store ||
      *action_operation == mojom::TaskActionOperationKind::kHistoryRecent ||
      *action_operation == mojom::TaskActionOperationKind::kBookmarksList ||
      *action_operation == mojom::TaskActionOperationKind::kOpenTabsList;
  if (input.has_task_store_binding != task_store_operation ||
      input.task_store_has_query != searches_store ||
      input.task_store_has_query != !input.task_store_query.empty() ||
      input.task_store_query.size() > mojom::kMaxTaskStoreQueryBytes ||
      (task_store_operation
           ? (input.task_store_limit == 0u ||
              input.task_store_limit > mojom::kMaxTaskStoreResults ||
              input.has_node_id ||
              *action_class != mojom::PolicyActionClass::kProfileStoreRead ||
              *postcondition !=
                  mojom::TaskActionPostcondition::kStoreRowsListed)
           : (input.task_store_limit != 0u ||
              *action_class == mojom::PolicyActionClass::kProfileStoreRead ||
              *postcondition ==
                  mojom::TaskActionPostcondition::kStoreRowsListed))) {
    return std::nullopt;
  }
  const bool has_complete_task_tab_target =
      input.has_task_tab_target &&
      ValidIdentifier(input.task_tab_target_tab_id) &&
      ValidIdentifier(input.task_tab_target_frame_id) &&
      ValidIdentifier(input.task_tab_target_page_epoch) &&
      input.task_tab_target_graph_revision != 0u;
  if (input.has_task_tab_binding != task_tab_operation ||
      input.has_task_download_binding != task_download_operation ||
      input.has_task_download_binding !=
          !input.task_download_browser_session_id.empty() ||
      (task_download_operation !=
       ValidIdentifier(input.task_download_browser_session_id)) ||
      cancels_download != !input.task_download_id.empty() ||
      (cancels_download && !ValidIdentifier(input.task_download_id)) ||
      input.has_task_tab_target != (activates_tab || closes_tab) ||
      (task_tab_operation !=
       ValidIdentifier(input.task_tab_browser_session_id)) ||
      input.has_task_tab_binding !=
          !input.task_tab_browser_session_id.empty() ||
      (input.has_task_tab_target != has_complete_task_tab_target) ||
      (!input.has_task_tab_target &&
       (!input.task_tab_target_tab_id.empty() ||
        !input.task_tab_target_frame_id.empty() ||
        !input.task_tab_target_page_epoch.empty() ||
        input.task_tab_target_graph_revision != 0u)) ||
      (lists_tabs &&
       (*action_class != mojom::PolicyActionClass::kObservePage ||
        *postcondition != mojom::TaskActionPostcondition::kTaskTabsListed)) ||
      (activates_tab &&
       (*action_class != mojom::PolicyActionClass::kMoveFocus ||
        *postcondition != mojom::TaskActionPostcondition::kTaskTabActive)) ||
      (closes_tab &&
       (*action_class != mojom::PolicyActionClass::kCreateTaskTab ||
        *postcondition != mojom::TaskActionPostcondition::kTaskTabAbsent)) ||
      (starts_download &&
       (*action_class != mojom::PolicyActionClass::kStartDownload ||
        *postcondition != mojom::TaskActionPostcondition::kDownloadStarted)) ||
      (lists_downloads &&
       (*action_class != mojom::PolicyActionClass::kObservePage ||
        *postcondition !=
            mojom::TaskActionPostcondition::kObservationCaptured)) ||
      (cancels_download &&
       (*action_class != mojom::PolicyActionClass::kStartDownload ||
        *postcondition !=
            mojom::TaskActionPostcondition::kDownloadCancelled))) {
    return std::nullopt;
  }
  std::optional<mojom::TaskActionInputPtr> action_input =
      ActionInput(input, *action_operation);
  if (!action_input) {
    return std::nullopt;
  }
  auto out = mojom::TaskActionEffect::New();
  out->action_id = std::string(input.action_id);
  out->proposal_digest = std::string(input.proposal_digest);
  out->idempotency_key = std::string(input.action_idempotency_key);
  out->capability_id = std::string(input.capability_id);
  out->dispatch_id = std::string(input.dispatch_id);
  out->document = mojom::TaskFrozenDocument::New(
      std::string(input.frame_id), std::string(input.page_epoch),
      input.graph_revision, std::string(input.normalized_origin),
      input.has_opaque_origin_id
          ? std::make_optional(std::string(input.opaque_origin_id))
          : std::nullopt);
  out->executable = mojom::TaskExecutableAction::New();
  out->executable->action_class = *action_class;
  out->executable->operation_kind = *action_operation;
  out->executable->canonical_intent.assign(input.canonical_intent.begin(),
                                           input.canonical_intent.end());
  out->executable->tool_name = std::string(input.tool_name);
  out->executable->tab_id = std::string(input.tab_id);
  out->executable->input = std::move(*action_input);
  if (task_tab_operation) {
    out->executable->task_tab = mojom::TaskTabActionBinding::New();
    out->executable->task_tab->browser_session_id =
        std::string(input.task_tab_browser_session_id);
    if (input.has_task_tab_target) {
      out->executable->task_tab->target = mojom::TaskTabDocumentTarget::New(
          std::string(input.task_tab_target_tab_id),
          std::string(input.task_tab_target_frame_id),
          std::string(input.task_tab_target_page_epoch),
          input.task_tab_target_graph_revision);
    }
  }
  if (task_download_operation) {
    out->executable->task_download = mojom::TaskDownloadActionBinding::New();
    out->executable->task_download->browser_session_id =
        std::string(input.task_download_browser_session_id);
    if (cancels_download) {
      out->executable->task_download->download_id =
          std::string(input.task_download_id);
    }
  }
  if (task_store_operation) {
    out->executable->task_store = mojom::TaskStoreActionBinding::New();
    out->executable->task_store->limit = input.task_store_limit;
    if (input.task_store_has_query) {
      out->executable->task_store->query = std::string(input.task_store_query);
    }
  }
  if (input.has_node_id) {
    if (!ValidIdentifier(input.node_id)) {
      return std::nullopt;
    }
    out->executable->node_id = std::string(input.node_id);
  }
  if (input.has_destination_origin) {
    if (input.destination_origin.empty() ||
        input.destination_origin.size() > mojom::kMaxNormalizedOriginBytes) {
      return std::nullopt;
    }
    out->executable->destination_origin = std::string(input.destination_origin);
  }
  if (input.has_destination_address) {
    if (input.destination_address.empty() ||
        input.destination_address.size() > mojom::kMaxDestinationAddressBytes) {
      return std::nullopt;
    }
    out->executable->destination_address =
        std::string(input.destination_address);
  }
  if (input.has_operand_handle) {
    if (!ValidIdentifier(input.operand_handle)) {
      return std::nullopt;
    }
    out->executable->operand_handle = std::string(input.operand_handle);
  }
  if (input.has_transient_search_query) {
    out->executable->transient_search_query =
        std::string(input.transient_search_query);
  }
  for (uint8_t precondition : input.preconditions) {
    const auto value = ClosedEnum(
        precondition, mojom::TaskActionPrecondition::kDestinationUnchanged);
    if (!value) {
      return std::nullopt;
    }
    out->preconditions.push_back(*value);
  }
  out->postcondition = *postcondition;
  if (*action_class == mojom::PolicyActionClass::kObservePage && !lists_tabs &&
      !lists_downloads) {
    const auto scope = ClosedEnum(input.observation_scope,
                                  mojom::ObservationScope::kSelectedSources);
    if (!scope || input.observation_max_bytes == 0u ||
        input.observation_max_nodes == 0u ||
        input.observation_max_text_bytes == 0u ||
        input.observation_max_frames == 0u ||
        input.observation_deadline_ms == 0u) {
      return std::nullopt;
    }
    out->observation = mojom::TaskObservationBounds::New(
        *scope, input.observation_max_bytes, input.observation_max_nodes,
        input.observation_max_text_bytes, input.observation_max_frames,
        input.observation_deadline_ms);
  } else if (input.observation_max_bytes != 0u ||
             input.observation_max_nodes != 0u ||
             input.observation_max_text_bytes != 0u ||
             input.observation_max_frames != 0u ||
             input.observation_deadline_ms != 0u) {
    return std::nullopt;
  }
  return out;
}

}  // namespace taffy::core_service_internal
