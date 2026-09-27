// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action.h"

#include <optional>
#include <string>

#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

// Whether `value` spells `parsed` the way GURL does, allowing the one
// difference a person or a model produces by hand: an address naming an origin
// and nothing else. GURL canonicalizes `https://example.test` to
// `https://example.test/`, and requiring the caller to have typed the slash
// made every such address unreadable here — so `HttpAddressOrigin` answered
// absent, and a model naming a site it knew was refused before policy saw it.
// Nothing else is admitted: any other disagreement between the spelling and
// the canonical form is still refused, because an address that does not say
// what it resolves to is exactly what this check exists to catch.
bool SpellingIsCanonical(const GURL& parsed, const std::string& value) {
  const std::string& canonical = parsed.spec();
  return canonical == value ||
         (canonical.size() == value.size() + 1u && canonical.back() == '/' &&
          parsed.path() == "/" && !parsed.has_query() &&
          !parsed.has_ref() && canonical.compare(0, value.size(), value) == 0);
}

std::optional<std::string> ParseHttpAddressOrigin(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxDestinationAddressBytes) {
    return std::nullopt;
  }
  const GURL parsed(value);
  if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS() ||
      parsed.has_username() || parsed.has_password() ||
      !SpellingIsCanonical(parsed, value)) {
    return std::nullopt;
  }
  const url::Origin origin = url::Origin::Create(parsed);
  if (origin.opaque()) {
    return std::nullopt;
  }
  return origin.Serialize();
}

}  // namespace

bool TaskOperationMatchesClassAndTool(mojom::TaskActionOperationKind operation,
                                      mojom::PolicyActionClass action_class,
                                      const std::string& tool_name) {
  switch (operation) {
    case mojom::TaskActionOperationKind::kNavigate:
      return action_class == mojom::PolicyActionClass::kOpenLink &&
             tool_name == "browser.navigate";
    case mojom::TaskActionOperationKind::kHistoryBack:
      return action_class == mojom::PolicyActionClass::kControlTab &&
             tool_name == "browser.back";
    case mojom::TaskActionOperationKind::kHistoryForward:
      return action_class == mojom::PolicyActionClass::kControlTab &&
             tool_name == "browser.forward";
    case mojom::TaskActionOperationKind::kReload:
      return action_class == mojom::PolicyActionClass::kControlTab &&
             tool_name == "browser.reload";
    case mojom::TaskActionOperationKind::kStopLoading:
      return action_class == mojom::PolicyActionClass::kControlTab &&
             tool_name == "browser.stop_loading";
    case mojom::TaskActionOperationKind::kSearch:
      return action_class == mojom::PolicyActionClass::kOpenLink &&
             tool_name == "browser.search";
    case mojom::TaskActionOperationKind::kLinkOpen:
      return action_class == mojom::PolicyActionClass::kOpenLink &&
             tool_name == "browser.link.open";
    case mojom::TaskActionOperationKind::kTabsOpen:
      return action_class == mojom::PolicyActionClass::kCreateTaskTab &&
             tool_name == "browser.tabs.open";
    case mojom::TaskActionOperationKind::kTabsList:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.tabs.list";
    case mojom::TaskActionOperationKind::kTabsActivate:
      return action_class == mojom::PolicyActionClass::kMoveFocus &&
             tool_name == "browser.tabs.activate";
    case mojom::TaskActionOperationKind::kTabsClose:
      return action_class == mojom::PolicyActionClass::kCreateTaskTab &&
             tool_name == "browser.tabs.close";
    case mojom::TaskActionOperationKind::kDomQuery:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.dom.query";
    case mojom::TaskActionOperationKind::kDomRead:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.dom.read";
    case mojom::TaskActionOperationKind::kDomClick:
      return action_class == mojom::PolicyActionClass::kSyntheticClick &&
             tool_name == "browser.dom.click";
    case mojom::TaskActionOperationKind::kDomFocus:
      return action_class == mojom::PolicyActionClass::kMoveFocus &&
             tool_name == "browser.dom.focus";
    case mojom::TaskActionOperationKind::kDomScroll:
      return action_class == mojom::PolicyActionClass::kScrollIntoView &&
             tool_name == "browser.dom.scroll";
    case mojom::TaskActionOperationKind::kFormInspect:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.form.inspect";
    case mojom::TaskActionOperationKind::kFormFill:
      return action_class == mojom::PolicyActionClass::kFillField &&
             tool_name == "browser.form.fill";
    case mojom::TaskActionOperationKind::kFormSelect:
      return action_class == mojom::PolicyActionClass::kSelectOption &&
             tool_name == "browser.form.select";
    case mojom::TaskActionOperationKind::kFormToggle:
      return action_class == mojom::PolicyActionClass::kToggleControl &&
             tool_name == "browser.form.toggle";
    case mojom::TaskActionOperationKind::kFormSubmit:
      return action_class == mojom::PolicyActionClass::kSubmitForm &&
             tool_name == "browser.form.submit";
    case mojom::TaskActionOperationKind::kSelectionRead:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.selection.read";
    case mojom::TaskActionOperationKind::kImageDescribe:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "page.images.describe";
    case mojom::TaskActionOperationKind::kImageReadText:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "page.images.read_text";
    case mojom::TaskActionOperationKind::kVideoInspect:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "page.video.inspect";
    case mojom::TaskActionOperationKind::kPdfInspect:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "page.pdf.inspect";
    case mojom::TaskActionOperationKind::kPageScreenshotInspect:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "page.screenshot.inspect";
    case mojom::TaskActionOperationKind::kDownloadStart:
      return action_class == mojom::PolicyActionClass::kStartDownload &&
             (tool_name == "browser.download.start" ||
              tool_name == "browser.download.from_link");
    case mojom::TaskActionOperationKind::kDownloadList:
      return action_class == mojom::PolicyActionClass::kObservePage &&
             tool_name == "browser.download.list";
    case mojom::TaskActionOperationKind::kDownloadCancel:
      return action_class == mojom::PolicyActionClass::kStartDownload &&
             tool_name == "browser.download.cancel";
    case mojom::TaskActionOperationKind::kToolJob:
      return false;
    case mojom::TaskActionOperationKind::kLibrarySearch:
      return action_class == mojom::PolicyActionClass::kLibraryRead &&
             tool_name == "library.search";
    case mojom::TaskActionOperationKind::kLibrarySave:
      return action_class == mojom::PolicyActionClass::kLibraryWrite &&
             tool_name == "library.save";
    case mojom::TaskActionOperationKind::kLibraryRemove:
      return action_class == mojom::PolicyActionClass::kLibraryWrite &&
             tool_name == "library.remove";
    case mojom::TaskActionOperationKind::kMemorySearch:
      return action_class == mojom::PolicyActionClass::kMemoryRead &&
             tool_name == "memory.search";
    case mojom::TaskActionOperationKind::kMemorySave:
      return action_class == mojom::PolicyActionClass::kMemoryWrite &&
             tool_name == "memory.save";
    case mojom::TaskActionOperationKind::kMemoryUpdate:
      return action_class == mojom::PolicyActionClass::kMemoryWrite &&
             tool_name == "memory.update";
    case mojom::TaskActionOperationKind::kMemoryDelete:
      return action_class == mojom::PolicyActionClass::kMemoryWrite &&
             tool_name == "memory.delete";
    case mojom::TaskActionOperationKind::kHistorySearch:
      return action_class == mojom::PolicyActionClass::kProfileStoreRead &&
             tool_name == "history.search";
    case mojom::TaskActionOperationKind::kHistoryRecent:
      return action_class == mojom::PolicyActionClass::kProfileStoreRead &&
             tool_name == "history.recent";
    case mojom::TaskActionOperationKind::kBookmarksSearch:
      return action_class == mojom::PolicyActionClass::kProfileStoreRead &&
             tool_name == "bookmarks.search";
    case mojom::TaskActionOperationKind::kBookmarksList:
      return action_class == mojom::PolicyActionClass::kProfileStoreRead &&
             tool_name == "bookmarks.list";
    case mojom::TaskActionOperationKind::kOpenTabsList:
      return action_class == mojom::PolicyActionClass::kProfileStoreRead &&
             tool_name == "open_tabs.list";
  }
  return false;
}

bool IsAdmittedPageActionClass(mojom::PolicyActionClass action_class) {
  switch (action_class) {
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kScrollIntoView:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryRead:
    case mojom::PolicyActionClass::kMemoryWrite:
    case mojom::PolicyActionClass::kControlTab:
    case mojom::PolicyActionClass::kProfileStoreRead:
      return true;
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
    case mojom::PolicyActionClass::kExecuteToolJob:
      return false;
  }
  return false;
}

bool TaskActionClassMutates(mojom::PolicyActionClass action_class) {
  switch (action_class) {
    case mojom::PolicyActionClass::kObservePage:
    case mojom::PolicyActionClass::kScrollIntoView:
    // A tool job computes over material the task already holds; it changes no
    // page and sends nothing, mirroring task-engine's non-mutating class.
    case mojom::PolicyActionClass::kExecuteToolJob:
    case mojom::PolicyActionClass::kLibraryRead:
    case mojom::PolicyActionClass::kMemoryRead:
    // A store read leaves History, Bookmarks and the tab strip as they were.
    case mojom::PolicyActionClass::kProfileStoreRead:
      return false;
    case mojom::PolicyActionClass::kSyntheticClick:
    case mojom::PolicyActionClass::kOpenLink:
    case mojom::PolicyActionClass::kCreateTaskTab:
    case mojom::PolicyActionClass::kMoveFocus:
    case mojom::PolicyActionClass::kFillField:
    case mojom::PolicyActionClass::kSelectOption:
    case mojom::PolicyActionClass::kToggleControl:
    case mojom::PolicyActionClass::kSubmitForm:
    case mojom::PolicyActionClass::kStartDownload:
    case mojom::PolicyActionClass::kUploadFile:
    case mojom::PolicyActionClass::kSendMessage:
    case mojom::PolicyActionClass::kPurchase:
    case mojom::PolicyActionClass::kExtractCredential:
    case mojom::PolicyActionClass::kBypassAccessControl:
    case mojom::PolicyActionClass::kLibraryWrite:
    case mojom::PolicyActionClass::kMemoryWrite:
    case mojom::PolicyActionClass::kControlTab:
      return true;
  }
  return true;
}

bool IsValidInTabNavigateAction(const mojom::TaskActionEffect& action) {
  if (action.observation || !action.executable || action.executable->node_id ||
      !action.executable->destination_origin ||
      action.executable->operand_handle ||
      action.executable->transient_search_query ||
      action.executable->task_tab || action.executable->task_download ||
      action.executable->task_store ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      !IsTaskNavigationOperation(action.executable->operation_kind) ||
      action.preconditions.size() != 3u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast ||
      action.preconditions[2] !=
          mojom::TaskActionPrecondition::kDestinationUnchanged ||
      action.postcondition !=
          mojom::TaskActionPostcondition::kDocumentNavigated) {
    return false;
  }
  return action.executable->destination_address &&
         HttpAddressOriginEquals(*action.executable->destination_address,
                                 *action.executable->destination_origin);
}

bool IsValidTaskTabControlAction(const mojom::TaskActionEffect& action) {
  if (action.observation || !action.document || !action.executable ||
      action.executable->node_id || action.executable->destination_origin ||
      action.executable->destination_address ||
      action.executable->operand_handle ||
      action.executable->transient_search_query ||
      action.executable->task_tab || action.executable->task_download ||
      action.executable->task_store ||
      !IsTaskTabControlOperation(action.executable->operation_kind) ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      action.preconditions.size() != 2u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast) {
    return false;
  }
  const mojom::TaskActionPostcondition expected =
      action.executable->operation_kind ==
              mojom::TaskActionOperationKind::kReload
          ? mojom::TaskActionPostcondition::kPageReloaded
      : action.executable->operation_kind ==
              mojom::TaskActionOperationKind::kStopLoading
          ? mojom::TaskActionPostcondition::kLoadingStopped
          : mojom::TaskActionPostcondition::kDocumentNavigated;
  return action.postcondition == expected;
}

bool IsValidTaskOwnedBrowserAction(const mojom::TaskActionEffect& action) {
  if (action.observation || !action.executable || action.executable->node_id ||
      !action.executable->destination_origin ||
      !action.executable->destination_address ||
      !HttpAddressOriginEquals(*action.executable->destination_address,
                               *action.executable->destination_origin) ||
      !TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                        action.executable->action_class,
                                        action.executable->tool_name) ||
      action.preconditions.size() != 3u ||
      action.preconditions[0] !=
          mojom::TaskActionPrecondition::kDocumentUnchanged ||
      action.preconditions[1] !=
          mojom::TaskActionPrecondition::kGraphRevisionAtLeast ||
      action.preconditions[2] !=
          mojom::TaskActionPrecondition::kDestinationUnchanged ||
      action.postcondition !=
          mojom::TaskActionPostcondition::kDocumentNavigated) {
    return false;
  }
  if (IsTaskSearchOperation(action.executable->operation_kind)) {
    return action.executable->action_class ==
               mojom::PolicyActionClass::kOpenLink &&
           action.executable->operand_handle &&
           action.executable->transient_search_query &&
           CanonicalSearchIntentMatches(
               action.executable->canonical_intent, action.executable->tab_id,
               *action.executable->operand_handle,
               *action.executable->transient_search_query);
  }
  if (IsTaskTabOpenOperation(action.executable->operation_kind)) {
    return action.executable->action_class ==
               mojom::PolicyActionClass::kCreateTaskTab &&
           !action.executable->operand_handle &&
           !action.executable->transient_search_query &&
           CanonicalTabsOpenIntentMatches(
               action.executable->canonical_intent, action.executable->tab_id,
               *action.executable->destination_address);
  }
  return false;
}

bool IsValidObservedLinkOpenAction(const mojom::TaskActionEffect& action) {
  return !action.observation && action.document && action.executable &&
         action.executable->node_id &&
         IsIdentifier(*action.executable->node_id) &&
         action.executable->destination_origin &&
         action.executable->destination_address &&
         HttpAddressOriginEquals(*action.executable->destination_address,
                                 *action.executable->destination_origin) &&
         !action.executable->operand_handle &&
         !action.executable->transient_search_query &&
         IsTaskLinkOpenOperation(action.executable->operation_kind) &&
         TaskOperationMatchesClassAndTool(action.executable->operation_kind,
                                          action.executable->action_class,
                                          action.executable->tool_name) &&
         action.preconditions.size() == 4u &&
         action.preconditions[0] ==
             mojom::TaskActionPrecondition::kDocumentUnchanged &&
         action.preconditions[1] ==
             mojom::TaskActionPrecondition::kGraphRevisionAtLeast &&
         action.preconditions[2] ==
             mojom::TaskActionPrecondition::kNodePresent &&
         action.preconditions[3] ==
             mojom::TaskActionPrecondition::kDestinationUnchanged &&
         action.postcondition ==
             mojom::TaskActionPostcondition::kDocumentNavigated &&
         CanonicalLinkOpenIntentMatchesDocument(
             action.executable->canonical_intent, action.executable->tab_id,
             action.document->frame_id, action.document->page_epoch,
             action.document->graph_revision, *action.executable->node_id,
             action.document->normalized_origin);
}

bool HttpAddressOriginEquals(const std::string& address,
                             const std::string& origin) {
  const std::optional<std::string> parsed = ParseHttpAddressOrigin(address);
  return parsed && *parsed == origin;
}

std::optional<std::string> HttpAddressOrigin(const std::string& address) {
  return ParseHttpAddressOrigin(address);
}

}  // namespace taffy
