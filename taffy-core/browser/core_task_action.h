// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_ACTION_H_
#define TAFFY_BROWSER_CORE_TASK_ACTION_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
}

namespace taffy {

// Page-scoped M3 classes this build will evaluate and, for exact-node scroll,
// typed disclosure activation, and same-origin in-tab navigation, dispatch.
// Observation stays on its own executor. Everything else is still refused at
// the policy gate rather than rebound.
bool IsAdmittedPageActionClass(
    core_service::mojom::PolicyActionClass action_class);

// The exact operation/class/tool tuple the typed Rust intent is allowed to
// project into this browser build. This is intentionally narrower than the
// wire enumeration: an operation with no reviewed executor is refused instead
// of being treated as another member of the same broad policy class.
bool TaskOperationMatchesClassAndTool(
    core_service::mojom::TaskActionOperationKind operation,
    core_service::mojom::PolicyActionClass action_class,
    const std::string& tool_name);

// Validates the closed, content-free input shape for every operation. Form
// inputs are additionally rebound to the exact canonical intent, tab, node,
// and supplied-value position or toggle state. No raw field bytes or
// browser-minted reference name is accepted on this seam.
bool TaskActionInputMatchesOperationAndCanonical(
    const core_service::mojom::TaskActionInput* input,
    core_service::mojom::TaskActionOperationKind operation,
    const std::vector<uint8_t>& canonical_intent,
    const std::string& tab_id,
    const std::optional<std::string>& node_id);

bool IsTaskObservationOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskHistoryOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskTabControlOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskNavigationOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskSearchOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskTabOpenOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskTabOperation(core_service::mojom::TaskActionOperationKind operation);
// The moves that land their own tab somewhere, and so may carry a source the
// browser discovered for it. The core's terminal decoder admits exactly this
// set; a terminal that names a source on any other operation is refused whole.
bool TaskActionOperationSettlesItsOwnTab(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskDownloadOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskLibraryOperation(
    core_service::mojom::TaskActionOperationKind operation);
// The five reads of a store the start attached (decision 0133), and the two
// of them that carry the person's words.
bool IsTaskStoreOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskStoreSearchOperation(
    core_service::mojom::TaskActionOperationKind operation);
bool IsTaskLinkOpenOperation(
    core_service::mojom::TaskActionOperationKind operation);
// The class a typed operation — a tab, store, Library or Memory read or
// write, none of them a page act — carries; nullopt for a page operation,
// whose class depends on what it does to the node.
std::optional<core_service::mojom::PolicyActionClass> TypedTaskOperationClass(
    core_service::mojom::TaskActionOperationKind operation);

// Matches `ActionClass::mutates` for the admitted page-scoped classes. A
// second table would drift; this switch is the C++ spelling of that one.
bool TaskActionClassMutates(
    core_service::mojom::PolicyActionClass action_class);

// Whether an exact-node scroll binding is well formed. Observation stays in
// `IsValidAction` beside its bounds; this is the sibling for the class that
// reaches `ActionDispatcher` through a node envelope.
bool IsValidNodeTargetedPageAction(
    const core_service::mojom::TaskActionEffect& action);

// The only policy classes this page-action path can translate, with no
// caller-owned default for an unsupported class.
std::optional<ActionType> PageActionTypeForClass(
    core_service::mojom::PolicyActionClass action_class);

// Narrows the Core effect claim to the concrete BIP claim this browser can
// verify. A pair without an exact mapping is unexecutable, never replaced by a
// convenient default postcondition.
std::optional<PostconditionKind> PagePostconditionForTaskAction(
    core_service::mojom::PolicyActionClass action_class,
    core_service::mojom::TaskActionPostcondition postcondition);

// Translates the content-free Core input into the exact BIP envelope input.
// Supplied positions become deterministic browser-owned value-reference names;
// toggle carries only its boolean; submit carries an explicit none. No raw
// field bytes exist on the Core seam.
std::optional<ActionInput> PageInputForTaskAction(
    const core_service::mojom::TaskActionEffect& action,
    ActionType action_type);

// Whether an in-tab `browser.navigate` binding is well formed. No node, an
// https address whose origin matches the granted destination, and a committed
// navigation postcondition.
bool IsValidInTabNavigateAction(
    const core_service::mojom::TaskActionEffect& action);

// Whether back, forward, reload, or stop-loading is the exact destination-free
// control of the frozen current tab. Live controller state is re-read only in
// the browser immediately before execution.
bool IsValidTaskTabControlAction(
    const core_service::mojom::TaskActionEffect& action);

// Whether Search or addressed TabsOpen is an exact, executable browser-owned
// binding. This includes the transient-query-to-canonical-digest check for
// Search and the exact canonical address check for TabsOpen.
bool IsValidTaskOwnedBrowserAction(
    const core_service::mojom::TaskActionEffect& action);

// Whether browser.link.open is bound to exactly one observed node and one
// browser-resolved HTTP(S) destination, with both facts required to remain
// unchanged until dispatch.
bool IsValidObservedLinkOpenAction(
    const core_service::mojom::TaskActionEffect& action);

// Validates the typed start/list download binding. Both operations are bound
// to the exact live document and browser session; start additionally binds one
// canonical HTTPS destination. The list path carries no observation body.
bool IsValidTaskDownloadAction(
    const core_service::mojom::TaskActionEffect& action);

bool TaskDownloadDestinationIsCurrent(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskActionEffect& action);

// Validates the typed attached-store binding (decision 0133): the class is
// ProfileStoreRead, the context document is exact, no node or destination is
// named, the row cap is within the wire's, and the two search kinds carry the
// resolved words whose digest the frozen canonical intent names.
bool IsValidTaskStoreAction(
    const core_service::mojom::TaskActionEffect& action);

// True when `address` is an http(s) URL whose tuple origin equals `origin`.
bool HttpAddressOriginEquals(const std::string& address,
                             const std::string& origin);
std::optional<std::string> HttpAddressOrigin(const std::string& address);

// Maps a verified, refused, or ambiguous dispatcher result onto the one
// correlated task-effect terminal. Unknown codes fail closed as refused.
core_service::mojom::TaskEffectCompletionStatus CompletionStatusForActionResult(
    ActionResultCode code);

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_ACTION_H_
