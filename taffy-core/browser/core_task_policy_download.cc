// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/core_task_policy.h"

namespace taffy {
namespace mojom = core_service::mojom;

bool TaskDownloadCanonicalMatchesSession(
    const mojom::TaskPolicyEffect& effect,
    const std::string& browser_session_id) {
  if (!IsTaskDownloadOperation(effect.operation_kind)) {
    return true;
  }
  if (effect.operation_kind == mojom::TaskActionOperationKind::kDownloadStart &&
      effect.tool_name == "browser.download.from_link") {
    const auto link = ReadCanonicalDownloadLinkIntent(effect.canonical_intent);
    return link && link->browser_session_id == browser_session_id;
  }
  const auto download =
      ReadCanonicalTaskDownloadIntent(effect.canonical_intent);
  return download && download->browser_session_id == browser_session_id;
}

bool TaskDownloadCanonicalMatchesEffect(const mojom::TaskPolicyEffect& effect) {
  if (!IsTaskDownloadOperation(effect.operation_kind)) {
    return true;
  }
  if (effect.operation_kind == mojom::TaskActionOperationKind::kDownloadStart &&
      effect.tool_name == "browser.download.from_link") {
    const auto link = ReadCanonicalDownloadLinkIntent(effect.canonical_intent);
    return link && effect.node_id && !effect.destination_address &&
           link->target.tab_id == effect.tab_id &&
           link->target.node_id == *effect.node_id;
  }
  const std::optional<CanonicalTaskDownloadIntent> parsed =
      ReadCanonicalTaskDownloadIntent(effect.canonical_intent);
  if (!parsed || parsed->context_tab_id != effect.tab_id) {
    return false;
  }
  switch (effect.operation_kind) {
    case mojom::TaskActionOperationKind::kDownloadStart:
      return parsed->operation_tag == 15u && parsed->destination_address &&
             effect.destination_address == parsed->destination_address;
    case mojom::TaskActionOperationKind::kDownloadList:
      return parsed->operation_tag == 16u && !parsed->destination_address &&
             !parsed->download_id && !effect.destination_address;
    case mojom::TaskActionOperationKind::kDownloadCancel:
      return parsed->operation_tag == 27u && !parsed->destination_address &&
             parsed->download_id && !effect.destination_address;
    default:
      return false;
  }
}

}  // namespace taffy
