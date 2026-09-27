// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_H_
#define TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

namespace taffy {

// Strictly re-reads the two live browser-operation projections from the
// frozen canonical Rust intent. Search's transient query is accepted only
// when its SHA-256 equals the opaque operand digest inside that intent;
// TabsOpen's address must be the exact optional-address payload. These are
// intentionally operation-specific: C++ does not reinterpret arbitrary Rust
// intents, it verifies only the two primitives it is about to execute.
bool CanonicalSearchIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                  const std::string& tab_id,
                                  const std::string& operand_handle,
                                  const std::string& transient_query);
bool CanonicalSearchQueryMatches(const std::vector<uint8_t>& canonical_intent,
                                 const std::string& tab_id,
                                 const std::string& transient_query);
// A typed bootstrap address can navigate only its already-owned blank tab.
// Its canonical operands must name that exact tab and address, with no new tab.
bool CanonicalInTabNavigateIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& destination_address);
bool CanonicalTabsOpenIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& destination_address);
// Back, forward, reload, and stop-loading are exact tab controls. Their
// durable intent contains only the closed operation tag and context tab; a
// browser history URL or current address is live browser state, never a value
// the isolated core is allowed to freeze and replay.
bool CanonicalTabControlIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& context_tab_id);

// Closed browser-session/document binding for browser.tabs.list/activate/close.
// The operation tag is part of the canonical Rust bytes (5/6/7); activate and
// close additionally carry the exact BIP tab/frame/epoch/revision tuple.
struct CanonicalTaskTabIntent {
  uint8_t operation_tag = 0u;
  std::string context_tab_id;
  std::string browser_session_id;
  std::optional<std::string> target_tab_id;
  std::optional<std::string> frame_id;
  std::optional<std::string> page_epoch;
  std::optional<uint64_t> graph_revision;
};

std::optional<CanonicalTaskTabIntent> ReadCanonicalTaskTabIntent(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalTaskTabListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id);
bool CanonicalTaskTabTargetIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& context_tab_id,
    const std::string& browser_session_id,
    const std::string& target_tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision);
// DownloadStart carries the exact destination and browser-session identity;
// DownloadList carries the same exact session without a destination. Neither
// parser accepts optional/trailing fields or repairs malformed UTF-8 bytes.
struct CanonicalTaskDownloadIntent {
  uint8_t operation_tag = 0u;
  std::string context_tab_id;
  std::optional<std::string> destination_address;
  std::string browser_session_id;
  std::optional<std::string> download_id;
};
std::optional<CanonicalTaskDownloadIntent> ReadCanonicalTaskDownloadIntent(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalDownloadStartIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& destination_address,
    const std::string& browser_session_id);
bool CanonicalDownloadListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id);
bool CanonicalDownloadCancelIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& context_tab_id,
    const std::string& browser_session_id,
    const std::string& download_id);
// Library intents are executed inside the sandboxed core. The browser policy
// seam re-reads their exact closed shape without receiving the search text or
// interpreting saved fact content.
bool CanonicalLibraryIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                   uint8_t operation_kind,
                                   const std::string& context_tab_id);
// Memory intents use only content-free opaque operand references. This
// browser-side check binds the closed operation, context tab, exact revision
// fields, and scope shape without receiving a query or statement byte.
bool CanonicalMemoryIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                  uint8_t operation_kind,
                                  const std::string& context_tab_id);
// A read of a store the start attached (decision 0133): family 251, one kind
// byte (0 history.search, 1 history.recent, 2 bookmarks.search,
// 3 bookmarks.list, 4 open_tabs.list) and the context tab. The two search
// kinds carry an opaque store-query operand and a row cap; the two list
// kinds a row cap; open tabs nothing more. The shape matcher binds kind and
// tab for the policy ask, which carries no words; the search matcher binds
// the resolved words by digest and handle at dispatch, and the list matcher
// the exact cap.
struct CanonicalStoreIntent {
  uint8_t kind = 0u;
  std::string context_tab_id;
  std::optional<std::string> operand_handle;
  std::optional<std::vector<uint8_t>> query_digest;
  std::optional<uint32_t> limit;
};
std::optional<CanonicalStoreIntent> ReadCanonicalStoreIntent(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalStoreIntentMatches(const std::vector<uint8_t>& canonical_intent,
                                 uint8_t kind,
                                 const std::string& context_tab_id);
bool CanonicalStoreSearchIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t kind,
    const std::string& context_tab_id,
    const std::string& operand_handle,
    const std::string& transient_query,
    uint32_t limit);
bool CanonicalStoreListIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t kind,
    const std::string& context_tab_id,
    uint32_t limit);
// DomQuery captures the current document and carries no executable node. Its
// optional within/role/opaque-text/limit filters nevertheless remain frozen in
// the canonical Rust intent and are validated here before document authority
// is granted; Rust applies them after the observation returns.
bool CanonicalDomQueryIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id);
// Complete, destination-free identity of one node observed by the browser.
// Bare node ids are never sufficient for an action: frame, document epoch,
// graph revision, and origin all participate in stale-handle refusal.
struct CanonicalObservedNodeHandle {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  uint64_t graph_revision = 0;
  std::string node_id;
  bool expected_origin_is_opaque = false;
  std::string expected_origin;
};
// A DOM activation names one exact observed node, and optionally the exact
// disclosure state it must reach. Other node states belong to narrower action
// classes; the absent case is an ordinary press, which claims nothing about a
// state because a page's own control is free to do anything at all.
struct CanonicalDomActivationIntent {
  CanonicalObservedNodeHandle target;
  std::optional<bool> expected_expanded;
};
std::optional<CanonicalDomActivationIntent> ReadCanonicalDomActivationIntent(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalDomActivationIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id);
bool CanonicalDomActivationIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin);
// DOM focus has one fixed outcome and therefore carries only the complete
// observed-node identity, under its own operation tag.
std::optional<CanonicalObservedNodeHandle> ReadCanonicalDomFocusHandle(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalDomFocusIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id);
bool CanonicalDomFocusIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin);
// Which clause of a press's or a focus's observed-node handle disagrees with
// the live document the policy gate is about to bind it to, as a compiled-in
// name, or nullptr when none does. Every clause is exact except the revision:
// a handle read at an earlier revision of this same document is admitted, and
// the gate binds it at its own revision, so the node having changed since is
// refused where the node is rather than the page having changed anywhere
// (decision 0188). A revision the browser has never reported is refused.
const char* ObservedNodeHandleMismatch(
    const CanonicalObservedNodeHandle& handle,
    const std::string& tab_id,
    const std::string& node_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& origin);
// Exact read-only page tools. FormInspect binds both the tab and form node;
// SelectionRead binds the tab and deliberately carries no node or text.
bool CanonicalFormInspectIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& form_node_id);
bool CanonicalFormSuppliedValueIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& tab_id,
    const std::string& field_node_id,
    const std::string& value_request_id,
    uint32_t supplied_value_index);
bool CanonicalFormToggleIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& field_node_id,
    bool checked);
bool CanonicalFormSubmitIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& submit_control_node_id);
bool CanonicalSelectionReadIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id);
// Exact media reads. Image description, image text reading, and video
// inspection bind one semantic node; PDF and screenshot-fallback inspection
// bind the current document and deliberately carry no node. `operation_tag`
// is the frozen task-engine canonical tag (18..21 or 26), not a Core Service
// enum ordinal.
bool CanonicalMediaReadIntentMatches(
    const std::vector<uint8_t>& canonical_intent,
    uint8_t operation_tag,
    const std::string& tab_id,
    const std::optional<std::string>& node_id);
// LinkOpen carries only the exact observed node identity. The destination is
// deliberately absent from durable Rust bytes and is resolved by the browser's
// current-snapshot registry before policy and again before dispatch.
using CanonicalLinkOpenHandle = CanonicalObservedNodeHandle;

// A fresh observed link, bound to the exact download-manager session. The
// destination remains in browser custody and never enters the saved intent.
struct CanonicalDownloadLinkIntent {
  CanonicalObservedNodeHandle target;
  std::string browser_session_id;
};
std::optional<CanonicalDownloadLinkIntent> ReadCanonicalDownloadLinkIntent(
    const std::vector<uint8_t>& canonical_intent);

std::optional<CanonicalLinkOpenHandle> ReadCanonicalLinkOpenHandle(
    const std::vector<uint8_t>& canonical_intent);
bool CanonicalLinkOpenIntentMatchesProjections(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& node_id);
bool CanonicalLinkOpenIntentMatchesDocument(
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::string& frame_id,
    const std::string& page_epoch,
    uint64_t graph_revision,
    const std::string& node_id,
    const std::string& expected_origin);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_CANONICAL_INTENT_H_
