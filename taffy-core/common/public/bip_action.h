// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_BIP_ACTION_H_
#define TAFFY_PUBLIC_BIP_ACTION_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_result.h"

// The action contract as it crosses into the browser process
// (protocol sections 11.2 to 11.7,
// taffy-core/contracts/bip/schema/action.schema.json).
//
// The task engine proposes. The policy engine authorizes. This header carries
// the authorized result into C++, and the browser broker is the last authority
// before anything touches a page: it re-checks every precondition against
// browser-owned state, consumes the capability, journals the intent, and only
// then dispatches.
//
// Nothing here is a secret. The capability's unforgeable token stays in the
// process that minted it; what travels here is a reference plus the decision
// evidence needed to consume it exactly once (domain model section 12.4).

namespace taffy {

// What an action does to a node. Members and order match the contract.
//
// kSetText, kSelectOption, kToggle and kSubmitForm write. Every one still goes
// through capability admission, exact live-node rechecks, journal-before-
// dispatch and the platform accessibility path; enum membership is never
// authorization.
enum class ActionType : uint8_t {
  kActivate = 0,
  kFocus = 1,
  kScrollIntoView = 2,
  kSetText = 3,
  kSelectOption = 4,
  kToggle = 5,
  kSubmitForm = 6,
};

// Closed-enum guard for values constructed inside C++. Mojo rejects unknown
// wire values first; this independently fails closed for casts and tests.
constexpr bool IsKnownActionType(ActionType type) {
  switch (type) {
    case ActionType::kSetText:
    case ActionType::kSelectOption:
    case ActionType::kToggle:
    case ActionType::kSubmitForm:
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
      return true;
  }
  return false;
}

// Commands the browser performs itself rather than asking a renderer to
// perform. They are deliberately NOT members of ActionType: the contract's
// ActionType describes what happens to a node, and these do not touch a node
// at all.
//
// Search is a browser command so a search query stays a transient,
// destination-bound browser input rather than becoming a value reference for
// an arbitrary page field. Commands still take a capability, a journalled
// intent and a verified postcondition; they simply never produce a
// RendererActionCommand.
enum class BrowserCommandType : uint8_t {
  kNavigate = 0,
  kOpenTaskTab = 1,
  kSearch = 2,
  // History traversal is browser-owned for the same reason navigation is:
  // it changes the committed document without targeting a renderer node.
  // Separate members keep direction inside the capability-bound digest
  // instead of carrying a string an executor has to interpret.
  kGoBack = 3,
  kGoForward = 4,
  // Navigates to the browser-resolved destination of one exact node handle.
  // Unlike kNavigate, no page-derived address entered through the core.
  kOpenObservedLink = 5,
  // Typed assistant-owned tab controls. Their exact browser-session and
  // document target live in the immutable task binding; these values identify
  // the journalled browser primitive without pretending it is a renderer
  // action or a navigation destination.
  kListTaskTabs = 6,
  kActivateTaskTab = 7,
  kCloseTaskTab = 8,
  // Typed assistant download primitives. A start is consequential and keeps
  // one durable dispatch identity across utility-process generations; a list
  // is a bounded, content-free read from the profile download manager.
  kStartDownload = 9,
  kListDownloads = 10,
  // Reverses only a task-owned start; the browser independently checks the
  // exact task/session/tab/GUID tuple before touching Chromium's item.
  kCancelDownload = 11,
  // Exact existing-tab navigation lifecycle controls. Neither carries a
  // destination or a renderer node; the browser rechecks current lifecycle
  // authority immediately before executing it.
  kReload = 12,
  kStopLoading = 13,
};

// The closed set used at every browser/storage boundary. Keep consumers on
// this table rather than repeating a numeric upper bound: the journal schema
// generator reads the same declaration and refuses a non-contiguous set, and
// the storage round-trip suite iterates every member.
inline constexpr std::array<BrowserCommandType, 14> kAllBrowserCommandTypes = {
    BrowserCommandType::kNavigate,      BrowserCommandType::kOpenTaskTab,
    BrowserCommandType::kSearch,        BrowserCommandType::kGoBack,
    BrowserCommandType::kGoForward,     BrowserCommandType::kOpenObservedLink,
    BrowserCommandType::kListTaskTabs,  BrowserCommandType::kActivateTaskTab,
    BrowserCommandType::kCloseTaskTab,  BrowserCommandType::kStartDownload,
    BrowserCommandType::kListDownloads, BrowserCommandType::kCancelDownload,
    BrowserCommandType::kReload,        BrowserCommandType::kStopLoading,
};

inline constexpr uint8_t kMaxBrowserCommandTypeWireValue =
    static_cast<uint8_t>(kAllBrowserCommandTypes.back());

// The commands whose journalled intent names the document it ran against.
//
// A second closed table rather than a rule over wire values, because the wire
// values are an append order and this is a property. The Python that renders
// the durable journal's SQL reads this array by name
// (`core_service_schema_validation.py`), and the switch below is what makes a
// new member a compile error; the two are proved equal at compile time
// immediately after it.
inline constexpr std::array<BrowserCommandType, 7>
    kDocumentBoundBrowserCommandTypes = {
        BrowserCommandType::kOpenObservedLink,
        BrowserCommandType::kListTaskTabs,
        BrowserCommandType::kActivateTaskTab,
        BrowserCommandType::kCloseTaskTab,
        BrowserCommandType::kStartDownload,
        BrowserCommandType::kListDownloads,
        BrowserCommandType::kCancelDownload,
};

// Whether a journalled intent for this command must name the document it ran
// against, one member at a time.
//
// This was `static_cast<uint8_t>(command) >= kOpenObservedLink`, which is the
// numeric upper bound the table above says not to write. `kReload` and
// `kStopLoading` were appended after the node-bearing members and so inherited
// a requirement their own declaration denies — "neither carries a destination
// or a renderer node" — and `AuthorizedBrowserCommand::source_handle` is set
// for `kOpenObservedLink` alone. So every reload a task proposed failed
// `IsValidIntent`, never reached the durable journal, came back
// `kDispatchFailed` and then `OutcomeUnknown`, and ended the errand as "Taffy
// stopped unexpectedly" — on a phone, on the one move a model reaches for when
// a page it is reading has gone stale.
//
// A switch makes the next member a compile error instead of a wrong answer,
// which is the whole reason the closed table beside it exists.
inline constexpr bool BrowserCommandRequiresDocument(
    BrowserCommandType command) {
  switch (command) {
    // One exact observed node, so the document it was observed in is part of
    // the claim the journal records.
    case BrowserCommandType::kOpenObservedLink:
    // The typed tab and download primitives are journalled by the browser,
    // against the task's bound document, which it is already holding.
    case BrowserCommandType::kListTaskTabs:
    case BrowserCommandType::kActivateTaskTab:
    case BrowserCommandType::kCloseTaskTab:
    case BrowserCommandType::kStartDownload:
    case BrowserCommandType::kListDownloads:
    case BrowserCommandType::kCancelDownload:
      return true;
    // A destination the browser resolved, and no renderer node.
    case BrowserCommandType::kNavigate:
    case BrowserCommandType::kOpenTaskTab:
    case BrowserCommandType::kSearch:
    case BrowserCommandType::kGoBack:
    case BrowserCommandType::kGoForward:
    // Lifecycle controls over the tab the task already holds. The browser
    // rechecks that authority immediately before executing them.
    case BrowserCommandType::kReload:
    case BrowserCommandType::kStopLoading:
      return false;
  }
  return true;
}


enum class ActionInputKind : uint8_t {
  kNone = 0,
  kText = 1,
  kOption = 2,
  kToggleState = 3,
};

// Who authored the bytes that enter a field, expressed as a shape rather than
// as a convention (protocol 0.8, action.schema.json ActionInput).
//
// A value is either **named** or **carried**, never both, and which of the two
// is permitted depends on where the message is:
//
//   * A proposal and an authorized envelope may name a value and may not carry
//     one. `value_reference` is the only form either may use, which is what
//     makes "the model never authors what enters a field" checkable: the
//     assistant runtime can say *put the value the person gave for the
//     national identifier into handle 7*, and there is no field on this struct
//     it could have spelled the number into.
//   * The narrowed one-use RendererActionCommand may carry a value and may not
//     name one. By then the browser has resolved the reference and spent it,
//     so a reference arriving in a sandboxed process is a name that outlived
//     its use - and a reference that outlives its use is a capability wearing
//     a different name.
//
// The two predicates below are those two sentences. Both are total, and both
// refuse rather than reconcile.
struct ActionInput {
  ActionInputKind kind = ActionInputKind::kNone;
  Sensitivity sensitivity = Sensitivity::kUnknownSensitive;
  // Names a value the browser holds; never any part of it. See
  // ValueReference in bip_identity.h for why not even a digest is carried.
  std::optional<ValueReference> value_reference;
  // Bounded text for a text-entry action, resolved by the browser from a
  // reference, and never a value classified as a credential. Populated only on
  // the command the browser hands a renderer.
  std::optional<std::string> text;
  std::optional<std::string> option_value;
  std::optional<bool> checked;
};

// True when an input is shaped as a proposal or an authorized envelope is
// allowed to shape one: it may name a value, and it may not carry bytes.
//
// `checked` is not bytes and is exempt on purpose. A toggle state is a
// decision about a control the page owns rather than content authored for it:
// there are two possible values and the page already names both, so naming one
// discloses nothing a reader of the page does not have.
inline bool IsNamedValueInputShape(const ActionInput& input) {
  if (input.text.has_value() || input.option_value.has_value()) {
    return false;
  }
  switch (input.kind) {
    case ActionInputKind::kNone:
      return !input.value_reference.has_value() && !input.checked.has_value();
    case ActionInputKind::kText:
    case ActionInputKind::kOption:
      return input.value_reference.has_value() &&
             input.value_reference->is_valid() && !input.checked.has_value();
    case ActionInputKind::kToggleState:
      return !input.value_reference.has_value() && input.checked.has_value();
  }
  // Fail closed on a value this build does not recognize.
  return false;
}

// Exact operation/input pairing on an authorized envelope. Literal bytes are
// already rejected by IsNamedValueInputShape; this second table prevents a
// validly-shaped value reference or boolean from being rebound to a different
// operation.
inline bool NamedInputMatchesAction(ActionType action,
                                    const std::optional<ActionInput>& input) {
  if (!IsKnownActionType(action)) {
    return false;
  }
  if (!input.has_value()) {
    return action == ActionType::kActivate || action == ActionType::kFocus ||
           action == ActionType::kScrollIntoView;
  }
  if (!IsNamedValueInputShape(*input)) {
    return false;
  }
  switch (action) {
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
    case ActionType::kSubmitForm:
      return input->kind == ActionInputKind::kNone;
    case ActionType::kSetText:
      return input->kind == ActionInputKind::kText;
    case ActionType::kSelectOption:
      return input->kind == ActionInputKind::kOption;
    case ActionType::kToggle:
      return input->kind == ActionInputKind::kToggleState;
  }
  return false;
}

// True when an input is shaped as the browser is allowed to hand a renderer:
// it carries the resolved bytes, and it names nothing.
inline bool IsResolvedValueInputShape(const ActionInput& input) {
  if (input.value_reference.has_value()) {
    return false;
  }
  switch (input.kind) {
    case ActionInputKind::kNone:
      return !input.text.has_value() && !input.option_value.has_value() &&
             !input.checked.has_value();
    case ActionInputKind::kText:
      return input.text.has_value() && !input.option_value.has_value() &&
             !input.checked.has_value();
    case ActionInputKind::kOption:
      return input.option_value.has_value() && !input.text.has_value() &&
             !input.checked.has_value();
    case ActionInputKind::kToggleState:
      return input.checked.has_value() && !input.text.has_value() &&
             !input.option_value.has_value();
  }
  return false;
}

enum class PrincipalKind : uint8_t {
  kAssistant = 0,
  kSkill = 1,
};

struct Principal {
  PrincipalKind kind = PrincipalKind::kAssistant;
  std::optional<SkillVersionId> skill_version_id;
};

enum class IdempotencyPolicy : uint8_t {
  kPureRead = 0,
  kIdempotentWrite = 1,
  kConditionallyIdempotent = 2,
  // Never automatically retried after an ambiguous dispatch (domain model
  // section 12.1).
  kNonIdempotent = 3,
};

// True when repeating an action under this policy could cause a second
// external side effect.
constexpr bool RepeatMayDuplicateEffect(IdempotencyPolicy policy) {
  return policy == IdempotencyPolicy::kNonIdempotent ||
         policy == IdempotencyPolicy::kConditionallyIdempotent;
}

enum class PreconditionKind : uint8_t {
  kExactPageEpoch = 0,
  kAcceptableGraphRevision = 1,
  kExactOrigin = 2,
  kAllowedRedirectSet = 3,
  kDocumentActive = 4,
  kNodeExists = 5,
  kNodeRoleUnchanged = 6,
  kNodeActionAvailable = 7,
  kNodeStateAsserted = 8,
  kNodeStateAbsent = 9,
  kExpectedDestination = 10,
  kExpectedValueDigest = 11,
  kNotSensitiveField = 12,
  kNoUserInteractionSinceLease = 13,
  kBudgetRemaining = 14,
  // Appended by the additive minor contract step that added the destination
  // checks. kDestinationClassAllowed carries no operand: the class table is
  // compiled into the browser and is never supplied by a message.
  kDestinationClassAllowed = 15,
  kContentTrustAtLeast = 16,
  // Appended by the additive minor contract step that split a consequential
  // effect into a prepare and a commit. kNoUndeclaredEgress likewise carries no
  // operand: the channels a task declared are held by the browser.
  kPreparedEffectUnchanged = 17,
  kNoUndeclaredEgress = 18,
};

// Who authored page content. Values match the frozen BIP enumeration exactly,
// but declaration order is not a trust order: ContentTrustAtLeast names a
// forbidden author label, and unknown authorship always refuses.
enum class ContentTrust : uint8_t {
  kUserAuthored = 0,
  kTaffyAuthored = 1,
  kFirstPartyDocument = 2,
  kUserGeneratedContent = 3,
  kThirdPartyEmbedded = 4,
  kModelAuthored = 5,
  kUnknownUntrusted = 6,
};

// Observed node state, mirroring the contract. Both a state and its negation
// exist so that "not asserted" stays distinguishable from "asserted false": an
// action policy that requires a state is not satisfied by silence.
enum class NodeState : uint8_t {
  kVisible = 0,
  kNotVisible = 1,
  kOffscreen = 2,
  kObscured = 3,
  kEnabled = 4,
  kDisabled = 5,
  kEditable = 6,
  kReadOnly = 7,
  kRequired = 8,
  kInvalid = 9,
  kChecked = 10,
  kUnchecked = 11,
  kMixed = 12,
  kSelected = 13,
  kExpanded = 14,
  kCollapsed = 15,
  kFocused = 16,
  kBusy = 17,
};

// How much of a URL policy permitted into a message.
enum class UrlDisclosure : uint8_t {
  kOriginOnly = 0,
  kOriginAndPath = 1,
  kFullUrl = 2,
};

struct UrlMetadata {
  Origin origin;
  UrlDisclosure disclosure = UrlDisclosure::kOriginOnly;
  std::optional<std::string> path;
  std::optional<std::string> url;
  bool has_query = false;
  bool has_fragment = false;

  friend bool operator==(const UrlMetadata&, const UrlMetadata&) = default;
};

struct Destination {
  UrlMetadata url_metadata;
  bool is_cross_origin = false;
  bool opens_new_tab = false;
  bool is_download = false;

  friend bool operator==(const Destination&, const Destination&) = default;
};

// A tagged condition. Only the members relevant to `kind` are populated.
struct Precondition {
  PreconditionKind kind = PreconditionKind::kNodeExists;
  std::optional<PageEpoch> page_epoch;
  std::optional<GraphRevision> min_graph_revision;
  std::optional<Origin> origin;
  std::vector<Origin> allowed_origins;
  // Role identifier from the shared vocabulary, carried as the contract's
  // numeric member value so this header stays free of the mojom types.
  std::optional<uint16_t> expected_role;
  std::optional<ActionType> expected_action_type;
  std::optional<NodeState> node_state;
  std::optional<Destination> expected_destination;
  std::optional<ContentDigest> expected_value_digest;
  std::optional<Sensitivity> max_sensitivity;
  std::optional<ContentTrust> min_content_trust;
};

// A browser-owned flow that a page can start but only the browser can confirm
// started. Form actions are live; these consequences remain independently
// attributed browser work and an action that declares an unwitnessed kind is
// refused.
enum class BrowserFlowKind : uint8_t {
  kDownload = 0,
  kFileChooser = 1,
  kPermissionPrompt = 2,
  kExternalIntent = 3,
};

// kNodeValueChanged is the content-free witness for live form writes.
// kBrowserFlowStarted remains usable only for a flow with an independent,
// dispatch-bound browser witness.
enum class PostconditionKind : uint8_t {
  kNoMutation = 0,
  kCommittedNavigation = 1,
  kNewTabCreated = 2,
  kSectionVisible = 3,
  kSearchResultState = 4,
  kNodeStateChanged = 5,
  kNodeValueChanged = 6,
  kBrowserFlowStarted = 7,
  // Browser-owned lifecycle evidence that the exact WebContents stopped
  // loading after this dispatch. It carries no destination or node operand.
  kLoadingStopped = 8,
  // The document moved past this dispatch: it committed a navigation, the
  // target node no longer exists, or an observation strictly newer than the
  // dispatch watermark arrived. The mirror of kNoMutation, and the only claim
  // an ordinary activation can make — the browser cannot know what a page's
  // own control does, so it says the page changed and nothing more.
  kDocumentAdvanced = 9,
};

// What an action claims it will cause.
//
// A postcondition has to carry an observable claim or it cannot be verified,
// and an action that cannot be verified is not an authorized action
// (protocol section 11.6). Which fields make a claim observable depends on the
// kind, so the rule lives in one function in
// //taffy/components/intelligence/content/postcondition_checks.h rather than in
// a comment here — but the fields it reads are all below, and every one of them
// exists because some kind needs it to say something checkable.
struct Postcondition {
  PostconditionKind kind = PostconditionKind::kNoMutation;
  std::optional<Destination> expected_destination;
  std::vector<Origin> allowed_origins;
  std::optional<NodeState> expected_node_state;
  // For kNodeValueChanged. A digest rather than the value, so that verifying a
  // change to a text field never requires the field's contents to exist
  // anywhere outside the renderer.
  std::optional<ContentDigest> expected_value_digest;
  // For kNewTabCreated. False means the new tab must have no reference back to
  // its opener, which is what a task tab opened for research requires.
  bool expects_opener_reference = false;
  // For kCommittedNavigation over an address the model typed. A site answers
  // the request it was given at a host of its own choosing inside its own
  // registrable domain — an apex that sends you to `www`, a regional host, a
  // staging host — and refusing that is refusing the site's own answer rather
  // than catching a redirect somewhere else. Both hops must be https and share
  // a port; a cross-site redirect is still contradicted, which is the whole
  // point of the class table.
  //
  // Off by default and set only where a typed address is built, because a
  // followed link and a browser-resolved search have an exact destination the
  // browser itself produced and need nothing widened.
  bool allows_registrable_domain_siblings = false;
  // For kCommittedNavigation over an address a page offered. A search engine's
  // result link is a redirector: its href is the engine's own address, and
  // following it is how the result reaches the site it names. The declared
  // destination is therefore where the *request* was sent and not where the
  // answer lives, so holding the commit to it refuses every result on every
  // engine that redirects — measured on a phone as
  // `declared=https://www.google.com committed=https://myaadhaar.uidai.gov.in
  // hops=2`, which is the errand arriving exactly where it meant to.
  //
  // What bounds the landing is not this check. `TaskNavigationAuthority`
  // decides every hop as it is requested — https throughout, and a cross-site
  // landing only where the destination-class table does not refuse it — and
  // `TaskNavigationThrottle` cancels the request outright otherwise, so a hop
  // it does not admit never commits at all. This flag stops the postcondition
  // from re-deciding, more narrowly, a question the authority beside it has
  // already answered (decision 0179).
  //
  // It applies only when the browser saw a redirect, and only when the chain
  // started at the declared destination: the claim is still "the request went
  // where we said, and the site answered from somewhere else". A followed link
  // that commits with no redirect at all is held to its own origin, and so is
  // every typed address — nobody offered those, so a landing elsewhere is the
  // site sending the task where it did not ask to go (decision 0176).
  bool allows_redirected_landing = false;
  // For kBrowserFlowStarted.
  std::optional<BrowserFlowKind> expected_flow;
  // The verifier's deadline. Clamped to the process ceiling like any other
  // budget.
  uint32_t timeout_ms = 0;
};

// Which observer corroborated an effect. kRendererAcknowledgement is recorded
// because it happened, not because it is sufficient: on its own it means
// DISPATCHED (protocol section 11.6).
enum class VerifierKind : uint8_t {
  kBrowserNavigationEvent = 0,
  kBrowserTabEvent = 1,
  kFreshSnapshot = 2,
  kDeltaObservation = 3,
  kRendererAcknowledgement = 4,
  // Appended: browser-owned, and the only witness to a request that was never
  // meant to happen.
  kBrowserNetworkEvent = 5,
};

struct PostconditionOutcome {
  Postcondition postcondition;
  bool satisfied = false;
  VerifierKind verifier = VerifierKind::kRendererAcknowledgement;
  std::optional<GraphRevision> observed_graph_revision;
  std::optional<std::string> detail_code;
};

// Browser-local copy of the discovery-only scope carried by a registered task
// capability. It never crosses into a renderer or a journal. Its sole purpose
// is to let the last browser-owned dispatch point re-check the exact opaque
// blank document and accepted browser session before starting a search.
struct TaskDiscoveryCapabilityBinding {
  TabId tab_id;
  FrameId frame_id;
  PageEpoch page_epoch;
  std::string opaque_origin_id;
  std::string browser_session_id;
  uint32_t remaining_new_source_cap = 0u;
};

// The capability, as handed to the browser process for consumption.
//
// The unforgeable token is not in this struct: what is here is the reference
// plus the evidence the ledger needs to admit it exactly once. The browser
// consumes the capability before it issues any renderer command, and never
// forwards any part of this struct onward (protocol section 4).
struct CapabilityGrant {
  CapabilityReference capability_reference;
  ActorLeaseId actor_lease_id;
  // Present when an approval was required. Absent means "no approval was
  // required", never "approval was skipped".
  std::optional<ApprovalReceiptReference> approval_receipt_reference;
  // A capability is short lived by construction; an expired one is never
  // renewed in place.
  MonotonicMillis expires_at_monotonic_ms = 0;
  uint32_t policy_version = 0;
  std::optional<TaskDiscoveryCapabilityBinding> task_discovery;
};

// The authorized dispatch envelope (protocol section 11.3).
//
// The first block mirrors the contract's AuthorizedActionEnvelope field for
// field. The second block is what the browser process additionally needs and
// the wire envelope does not carry, because the wire envelope is a message
// between two components that both already know it: the owning task, the
// reducer proposal's idempotency identity, the policy that decides whether an
// ambiguous outcome may be repeated, and the capability's own lifetime.
struct AuthorizedActionEnvelope {
  std::string schema_version;
  DispatchId dispatch_id;
  ActionId action_id;
  ContentDigest action_digest;
  // SHA-256 of the strict, content-free Rust ActionIntent encoding. The
  // proposal digest still identifies the reducer proposal; this second digest
  // prevents another operation in the same broad class from spending its
  // capability.
  std::array<uint8_t, 32> canonical_intent_digest = {};
  CapabilityReference capability_reference;
  std::optional<ApprovalReceiptReference> approval_receipt_reference;
  ActionType action_type = ActionType::kActivate;
  NodeHandle target_handle;
  std::optional<ActionInput> input;
  GraphRevision required_graph_revision = 0;
  std::vector<Precondition> preconditions;
  // At least one is required. An envelope that declares no expected effect is
  // rejected: an unverifiable action is not an authorized action
  // (protocol section 11.6).
  std::vector<Postcondition> expected_postconditions;
  MonotonicMillis absolute_deadline_monotonic_ms = 0;

  TaskId task_id;
  // Exact reducer-proposal identity carried by TaskActionEffect. This is
  // browser-local authority metadata: the renderer never receives it, but the
  // capability ledger must bind it to the key policy reviewed and minted.
  std::string idempotency_key;
  Principal principal;
  IdempotencyPolicy idempotency_policy = IdempotencyPolicy::kNonIdempotent;
  CapabilityGrant capability;
};

// A browser-owned command. Same authority path; only kOpenObservedLink carries
// a source node, because its destination is meaningful only for that exact
// observed handle.
struct AuthorizedBrowserCommand {
  std::string schema_version;
  DispatchId dispatch_id;
  ActionId action_id;
  ContentDigest action_digest;
  std::array<uint8_t, 32> canonical_intent_digest = {};
  CapabilityReference capability_reference;
  std::optional<ApprovalReceiptReference> approval_receipt_reference;
  BrowserCommandType command_type = BrowserCommandType::kNavigate;
  TabId tab_id;
  std::optional<NodeHandle> source_handle;
  // Exact capability-bound destination for every navigation-shaped command.
  std::string argument;
  // Live-only operand for kSearch. It is never journalled or copied into an
  // action record; the browser rechecks it against the canonical intent's
  // opaque digest immediately before the configured-engine path uses it.
  std::optional<std::string> transient_search_query;
  std::vector<Postcondition> expected_postconditions;
  MonotonicMillis absolute_deadline_monotonic_ms = 0;

  TaskId task_id;
  // Browser-local reducer-proposal identity. Task commands bind this to the
  // key policy reviewed; it is never forwarded to a renderer.
  std::string idempotency_key;
  Principal principal;
  IdempotencyPolicy idempotency_policy = IdempotencyPolicy::kNonIdempotent;
  CapabilityGrant capability;
};

// The single terminal result of one submitted action or browser command.
struct ActionResult {
  std::string schema_version;
  RequestId request_id;
  ActionId action_id;
  DispatchId dispatch_id;
  ActionResultCode result_code = ActionResultCode::kInternalError;
  // True when a side effect was actually attempted. False means nothing
  // reached a renderer or a navigation path at all, which is what makes a
  // refusal safe to treat as "nothing happened".
  bool dispatched = false;
  // Always true on this API: exactly one result is delivered per request.
  bool terminal = true;
  std::vector<VerifierKind> verified_by;
  std::vector<PostconditionOutcome> postcondition_outcomes;
  std::optional<PageEpoch> observed_page_epoch;
  std::optional<GraphRevision> observed_graph_revision;
  MonotonicMillis completed_at_monotonic_ms = 0;
  std::optional<std::string> detail_code;
  // Derived from the code and the idempotency policy so that no consumer
  // re-derives it and gets it wrong.
  bool repeat_may_duplicate_effect = true;
  // Which precondition refused the action, when one did.
  std::optional<PreconditionKind> failed_precondition;
};

}  // namespace taffy

#endif  // TAFFY_PUBLIC_BIP_ACTION_H_
