// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>

#include "taffy/browser/core_task_action.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace core_mojom = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= core_mojom::kMaxIdentifierBytes &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsCanonicalHttpsAddress(const std::string& address) {
  if (address.empty() ||
      address.size() > core_mojom::kMaxDestinationAddressBytes) {
    return false;
  }
  const GURL parsed(address);
  return parsed.is_valid() && parsed.SchemeIs("https") &&
         !parsed.has_username() && !parsed.has_password() &&
         parsed.spec() == address;
}

bool HasCommonDownloadShape(const core_mojom::TaskActionEffect& action) {
  return action.document && action.executable &&
         action.executable->task_download && !action.executable->task_tab &&
         !action.executable->task_store && !action.observation &&
         !action.executable->operand_handle &&
         !action.executable->transient_search_query &&
         IsIdentifier(action.executable->task_download->browser_session_id) &&
         action.preconditions.size() >= 2u &&
         action.preconditions[0] ==
             core_mojom::TaskActionPrecondition::kDocumentUnchanged &&
         action.preconditions[1] ==
             core_mojom::TaskActionPrecondition::kGraphRevisionAtLeast;
}

bool DownloadStartIntentMatches(const core_mojom::TaskActionEffect& action,
                                const std::string& browser_session_id) {
  if (action.executable->tool_name == "browser.download.start") {
    return !action.executable->node_id && action.preconditions.size() == 3u &&
           action.preconditions[2] ==
               core_mojom::TaskActionPrecondition::kDestinationUnchanged &&
           CanonicalDownloadStartIntentMatches(
               action.executable->canonical_intent, action.executable->tab_id,
               *action.executable->destination_address, browser_session_id);
  }
  const auto link =
      ReadCanonicalDownloadLinkIntent(action.executable->canonical_intent);
  return action.executable->tool_name == "browser.download.from_link" && link &&
         action.executable->node_id &&
         link->browser_session_id == browser_session_id &&
         link->target.tab_id == action.executable->tab_id &&
         link->target.node_id == *action.executable->node_id &&
         link->target.frame_id == action.document->frame_id &&
         link->target.page_epoch == action.document->page_epoch &&
         link->target.graph_revision == action.document->graph_revision &&
         link->target.expected_origin == action.document->normalized_origin &&
         action.preconditions.size() == 4u &&
         action.preconditions[2] ==
             core_mojom::TaskActionPrecondition::kNodePresent &&
         action.preconditions[3] ==
             core_mojom::TaskActionPrecondition::kDestinationUnchanged;
}

}  // namespace

bool TaskDownloadDestinationIsCurrent(
    content::BrowserContext* browser_context,
    const core_mojom::TaskActionEffect& action) {
  if (!action.executable) {
    return false;
  }
  if (action.executable->tool_name != "browser.download.from_link") {
    return true;
  }
  const auto link =
      ReadCanonicalDownloadLinkIntent(action.executable->canonical_intent);
  const auto current =
      link ? ResolveTaskObservedDownload(browser_context, link->target)
           : std::nullopt;
  return current && action.executable->destination_address == current;
}

bool IsValidTaskDownloadAction(const core_mojom::TaskActionEffect& action) {
  if (!HasCommonDownloadShape(action) ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name)) {
    return false;
  }
  const std::string& browser_session_id =
      action.executable->task_download->browser_session_id;
  switch (action.executable->operation_kind) {
    case core_mojom::TaskActionOperationKind::kDownloadStart:
      return action.executable->action_class ==
                 core_mojom::PolicyActionClass::kStartDownload &&
             !action.executable->task_download->download_id &&
             action.executable->destination_origin &&
             action.executable->destination_address &&
             IsCanonicalHttpsAddress(*action.executable->destination_address) &&
             HttpAddressOriginEquals(*action.executable->destination_address,
                                     *action.executable->destination_origin) &&
             action.postcondition ==
                 core_mojom::TaskActionPostcondition::kDownloadStarted &&
             DownloadStartIntentMatches(action, browser_session_id);
    case core_mojom::TaskActionOperationKind::kDownloadList:
      return !action.executable->node_id &&
             action.executable->action_class ==
                 core_mojom::PolicyActionClass::kObservePage &&
             !action.executable->task_download->download_id &&
             !action.executable->destination_origin &&
             !action.executable->destination_address &&
             action.preconditions.size() == 2u &&
             action.postcondition ==
                 core_mojom::TaskActionPostcondition::kObservationCaptured &&
             CanonicalDownloadListIntentMatches(
                 action.executable->canonical_intent, action.executable->tab_id,
                 browser_session_id);
    case core_mojom::TaskActionOperationKind::kDownloadCancel:
      return !action.executable->node_id &&
             action.executable->action_class ==
                 core_mojom::PolicyActionClass::kStartDownload &&
             action.executable->task_download->download_id &&
             IsIdentifier(*action.executable->task_download->download_id) &&
             !action.executable->destination_origin &&
             !action.executable->destination_address &&
             action.preconditions.size() == 2u &&
             action.postcondition ==
                 core_mojom::TaskActionPostcondition::kDownloadCancelled &&
             CanonicalDownloadCancelIntentMatches(
                 action.executable->canonical_intent, action.executable->tab_id,
                 browser_session_id,
                 *action.executable->task_download->download_id);
    default:
      return false;
  }
}

}  // namespace taffy
