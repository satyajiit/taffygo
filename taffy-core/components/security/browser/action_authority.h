// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_SECURITY_BROWSER_ACTION_AUTHORITY_H_
#define TAFFY_COMPONENTS_SECURITY_BROWSER_ACTION_AUTHORITY_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

// Actor leases and one-use capabilities, held in the browser process.
//
// Protocol section 4: "The browser broker MUST consume the capability before
// issuing a renderer command. A renderer receives a bounded one-use command
// ID, not a reusable capability secret." Both halves are implemented here.
//
// Why the ledger lives in the browser process rather than in the Rust policy
// engine that minted the capability: consumption has to happen on the same side
// of the process boundary as the side effect. If the core service marked a
// capability consumed and then asked the browser to act, a crash, a dropped
// message or a compromised core service would leave the two disagreeing about
// whether the effect happened. Here, the state transition and the dispatch are
// in the same function on the same thread.
//
// Why the lease registry lives here too: direct user input preempts the
// assistant's mutation authority, and Chromium delivers user input to the
// browser process. Preemption has to be synchronous with the input event to be
// worth anything (protocol section 17.4), so it cannot be a round trip.
//
// UI thread only.

namespace taffy {

enum class CapabilityAdmission : uint8_t {
  kAdmitted = 0,
  // Already admitted once. A capability is one use, and "one use" includes uses
  // that failed: an admitted capability is spent whether or not the action
  // worked, because the browser cannot prove the first attempt had no effect.
  kAlreadySpent = 1,
  kExpired = 2,
  // The envelope does not hash to the digest the policy engine authorized.
  // Something edited the action after authorization.
  kDigestMismatch = 3,
  kLeaseMissing = 4,
  kLeaseNotForThisTab = 5,
  kMalformed = 6,
};

// Maps an admission failure onto the result code the core service receives. One
// function so the mapping is reviewable in one place.
ActionResultCode AdmissionToResultCode(CapabilityAdmission admission);

// What one closed handover window observed.
//
// `person_input` is the evidence that a person actually acted, and `revoked`
// is the authority the handover took away. Both travel to the isolated core in
// the completion, and both exist for the same reason: an audit reading the
// journal afterwards has to be able to say which page changes were the
// assistant's and which were the person's.
struct HandoverEvidence {
  HandoverEvidence();
  HandoverEvidence(const HandoverEvidence&);
  HandoverEvidence(HandoverEvidence&&);
  HandoverEvidence& operator=(const HandoverEvidence&);
  HandoverEvidence& operator=(HandoverEvidence&&);
  ~HandoverEvidence();

  // Saturating count of committed person input, clamped at
  // kMaxCountedHandoverInput. See ActorLeaseRegistry::NoteHandoverInput for
  // what is counted and why the number stops.
  uint32_t person_input = 0;

  // The leases the handover revoked. A resumption may not name one of these.
  std::vector<ActorLeaseId> revoked;
};

// The exact BIP projection authorized by one registered read capability.
// This is derived from the closed operation kind and browser-held scope; the
// shared Core Service observation body cannot widen or substitute it.
enum class AuthorizedObservationKind : uint8_t {
  kDocument,
  kForm,
  kSelection,
  kImageDescription,
  kImageText,
  kVideo,
  kPdf,
  kPageScreenshot,
};

struct AuthorizedObservationTarget {
  AuthorizedObservationKind kind = AuthorizedObservationKind::kDocument;
  ObservationScope scope = ObservationScope::kDocument;
  std::optional<FormObservationRoot> form_root;
  // Present only for the three exact-node media reads. This is the same
  // browser-issued semantic identity carried by the grant; it never becomes
  // a selector, URL, or approximate geometric match.
  std::optional<MediaObservationRoot> media_root;

  bool is_valid() const {
    switch (kind) {
      case AuthorizedObservationKind::kDocument:
      case AuthorizedObservationKind::kPdf:
      case AuthorizedObservationKind::kPageScreenshot:
        return scope == ObservationScope::kDocument && !form_root &&
               !media_root;
      case AuthorizedObservationKind::kSelection:
        return scope == ObservationScope::kSelection && !form_root &&
               !media_root;
      case AuthorizedObservationKind::kForm:
        return scope == ObservationScope::kSection && form_root &&
               form_root->node_id.is_valid() &&
               form_root->minimum_graph_revision != 0u && !media_root;
      case AuthorizedObservationKind::kImageDescription:
      case AuthorizedObservationKind::kImageText:
      case AuthorizedObservationKind::kVideo:
        return scope == ObservationScope::kDocument && !form_root &&
               media_root && media_root->node_id.is_valid() &&
               media_root->minimum_graph_revision != 0u;
    }
    return false;
  }

  friend bool operator==(const AuthorizedObservationTarget&,
                         const AuthorizedObservationTarget&) = default;
};

// Where the handover input counter stops.
//
// Three, so a record can distinguish "nobody touched the page" from "one stray
// tap" from "the person worked here", and can distinguish nothing beyond that.
// The ceiling is the privacy half of the decision below; the classification is
// the honesty half.
inline constexpr uint32_t kMaxCountedHandoverInput = 3;

class ActorLeaseRegistry {
 public:
  ActorLeaseRegistry();
  ActorLeaseRegistry(const ActorLeaseRegistry&) = delete;
  ActorLeaseRegistry& operator=(const ActorLeaseRegistry&) = delete;
  ~ActorLeaseRegistry();

  ActorLeaseResult Issue(const ActorLeaseRequest& request, base::TimeTicks now);

  // Issues a short, read-only lease for one explicit selected-page request.
  // It is never a task lease and cannot overlap a browser mutation.
  ActorLeaseResult IssueDirectObservation(std::string direct_intent_id,
                                          const TabId& tab_id,
                                          uint32_t requested_duration_ms,
                                          base::TimeTicks now);

  // Activates the sole profile/generation authority domain. Advancing either
  // value revokes all leases before the new generation can register grants.
  void BeginGeneration(std::string profile_id, uint64_t generation);
  void RevokeGeneration(uint64_t generation);
  void RevokeTask(std::string_view task_id, uint64_t generation);

  // True when the lease exists, has not expired, has not been preempted, and
  // belongs to `tab_id`.
  bool IsValidFor(const ActorLeaseId& lease_id,
                  const TabId& tab_id,
                  base::TimeTicks now) const;
  bool IsValidForGrant(const ActorLeaseId& lease_id,
                       core_service::mojom::AuthoritySubjectKind subject_kind,
                       std::string_view authority_subject_id,
                       const TabId& tab_id,
                       std::string_view profile_id,
                       uint64_t generation,
                       base::TimeTicks grant_expires_at,
                       base::TimeTicks now) const;

  void Release(const ActorLeaseId& lease_id);

  // Revokes every lease on `tab_id` and returns the ids that were revoked, so
  // the caller can cancel their undispatched work and tell the core service.
  // Called on direct user input, when the user takes over, and on tab teardown.
  std::vector<ActorLeaseId> PreemptTab(const TabId& tab_id);

  // Revokes only taskless direct-read leases. Used for same-document
  // navigation, where task leases remain revision-revalidatable but a
  // selected-page request must never complete against the changed document.
  std::vector<ActorLeaseId> PreemptDirectObservations(const TabId& tab_id);

  // At most one active mutating lease exists per tab (domain model section
  // 12.3). Exposed for the test that proves it.
  bool HasMutatingLease(const TabId& tab_id, base::TimeTicks now) const;

  // --- Handover -------------------------------------------------------------
  //
  // A handover is the assistant stopping and giving the page to the person: it
  // is what remains once everything else has been refused. TaffyGo never
  // learns to recognise a test meant to prove a person is present, or a
  // one-time code, because kBypassAccessControl and kExtractCredential are
  // prohibited by action class and are refused before anything looks at them.
  // Refusal by exhaustion has no false negatives; a detector does.
  //
  // The window lives here, in the lease registry, because the two things it
  // needs are here: the authority to take away, and the operating-system input
  // that is the only honest evidence a person acted.

  // Opens the window on `tab_id`. Every lease on the tab is revoked first —
  // the assistant must not hold mutation authority over a tab a person is
  // about to type into — and the revoked identities are remembered so the
  // resumption can be checked against them. Returns the same list PreemptTab
  // would, so the caller can cancel undispatched work and tell the core.
  //
  // While the window is open, Issue refuses a mutating lease on this tab. A
  // revocation that could be undone by the next request would not be one, and
  // the refusal lives here rather than only in the isolated core's state
  // machine because this is the side that holds the leases.
  std::vector<ActorLeaseId> BeginHandover(const TabId& tab_id);

  // One input event the caller has already classified as committed person
  // input. Ignored unless a window is open on `tab_id`.
  //
  // WHAT COUNTS, AND WHY IT STOPS. This is the whole of the decision, recorded
  // where the counter is.
  //
  // Counted: one discrete committed input a person produced — a key press, or
  // a pointer or touch commit. Not counted: movement, wheel, scroll, hover,
  // pinch and fling, because looking at a challenge is not answering one and a
  // page that scrolls itself would otherwise manufacture the evidence that
  // somebody was there. Not counted: anything outside an open window, or in
  // any tab but the handed-over one; a count that could be accumulated before
  // the window opened is a count the page's own timing controls.
  //
  // Keys and pointer commits share one counter, and the counter saturates at
  // kMaxCountedHandoverInput. A person typing a one-time code or a password
  // into a handed-over tab produces exactly as many key events as the secret
  // has characters, so an exact per-kind count is the length of the secret,
  // written into a durable record by the one part of this product that
  // undertook never to see it. Merging the kinds makes six keystrokes
  // indistinguishable from two taps and four keystrokes; the ceiling makes
  // every secret long enough to matter indistinguishable from every other.
  //
  // It is evidence, never a gate. Nothing here decides that a challenge was
  // solved: a completed handover with a zero count is recorded and still
  // completes, because the person may have answered on another device or found
  // that nothing was needed. Deciding from the count would be the detector
  // this product refuses to build, reintroduced through the back door.
  void NoteHandoverInput(const TabId& tab_id);

  // The clamped count so far, or zero when no window is open on `tab_id`.
  uint32_t HandoverInputCount(const TabId& tab_id) const;

  // Closes the window and returns what it observed. Closing a window that was
  // never open returns empty evidence rather than inventing any.
  //
  // A resumption calls this BEFORE it asks for its new lease, because Issue
  // refuses a mutating lease while the window is open. That ordering is the
  // reason the evidence carries `revoked` by value: the identities outlive the
  // window, so the fresh lease can still be checked against them afterwards.
  HandoverEvidence EndHandover(const TabId& tab_id);

  // The first lease this open window recorded as revoked, or unset when the
  // window is missing or recorded none. Used to name `lease_before` when the
  // handover opened after RevokeTask had already marked every live lease.
  ActorLeaseId FirstHandoverRevokedLease(const TabId& tab_id) const;

  // Whether `lease_id` is one the open window on `tab_id` revoked.
  //
  // A resumption that named one would be the assistant carrying on under the
  // authority the handover took away, which leaves an audit unable to draw the
  // line between what the assistant did and what the person did. The isolated
  // core refuses such a completion too; this is the same refusal on the side
  // that actually holds the leases, because a check that exists only across a
  // process boundary is a check the browser can be talked out of.
  //
  // Answers only while the window is open. After EndHandover the same question
  // is asked of the `revoked` list in the evidence it returned.
  bool WasRevokedByHandover(const TabId& tab_id,
                            const ActorLeaseId& lease_id) const;

 private:
  struct LeaseRecord {
    core_service::mojom::AuthoritySubjectKind subject_kind =
        core_service::mojom::AuthoritySubjectKind::kTask;
    std::string authority_subject_id;
    TabId tab_id;
    bool mutating = false;
    base::TimeTicks expires_at;
    bool revoked = false;
    uint64_t generation = 0;
    std::string profile_id;
  };

  void PruneExpired(base::TimeTicks now);

  std::map<ActorLeaseId, LeaseRecord> leases_;

  // At most one open handover per tab, keyed by the tab it is about. Bounded
  // by the number of tabs, and an entry lives only between BeginHandover and
  // EndHandover.
  std::map<TabId, HandoverEvidence> handovers_;

  std::string active_profile_id_;
  uint64_t active_generation_ = 0;
};

class CapabilityLedger {
 public:
  CapabilityLedger();
  CapabilityLedger(const CapabilityLedger&) = delete;
  CapabilityLedger& operator=(const CapabilityLedger&) = delete;
  ~CapabilityLedger();

  void BeginGeneration(std::string profile_id, uint64_t generation);
  void RevokeGeneration(uint64_t generation);
  void RevokeTask(std::string_view task_id, uint64_t generation);

  core_service::mojom::CapabilityRegistrationStatus Register(
      const core_service::mojom::MintedCapabilityGrant& grant,
      const ActorLeaseRegistry& leases,
      base::TimeTicks now);

  CapabilityAdmission AdmitObservation(
      const core_service::mojom::PageObservationEffect& effect,
      std::string_view committed_origin,
      const ActorLeaseRegistry& leases,
      base::TimeTicks now,
      AuthorizedObservationTarget* authorized_target = nullptr);

  // Read-only preflight for a task observation before it is projected onto the
  // shared PageObservationEffect shape. The later AdmitObservation call still
  // performs the one-use transition; this check binds the typed operation and
  // canonical intent fields that the shared shape deliberately does not carry.
  bool TaskObservationMatchesRegisteredGrant(
      const core_service::mojom::TaskActionEffect& action) const;

  // Admits a capability exactly once and marks it in flight.
  //
  // `authorized_digest` is the digest the policy engine signed off, taken from
  // the envelope. `computed_digest` is recomputed by the caller from the
  // envelope it is actually about to dispatch — never copied out of the
  // envelope, or the check would compare the envelope against itself.
  //
  // Admission happens BEFORE any renderer command is built. That ordering is
  // the invariant: a renderer never sees a command whose capability had not
  // already been spent in this process.
  CapabilityAdmission Admit(const CapabilityGrant& grant,
                            const ContentDigest& authorized_digest,
                            const ContentDigest& computed_digest,
                            const ActorLeaseRegistry& leases,
                            const TabId& tab_id,
                            base::TimeTicks now);

  // The task-path sibling of `Admit`. Policy signed the reducer's proposal
  // digest; live frame/epoch/node facts were bound onto the grant at
  // evaluation. Comparing a BIP envelope digest against that proposal digest
  // would refuse every granted click, so this matches the grant field-by-field
  // the way `AdmitObservation` already does.
  CapabilityAdmission AdmitTaskAction(const AuthorizedActionEnvelope& envelope,
                                      const ActorLeaseRegistry& leases,
                                      const TabId& tab_id,
                                      base::TimeTicks now);

  // The browser-command sibling of `AdmitTaskAction`. Policy signed the
  // reducer's proposal digest for an in-tab navigation; there is no node
  // envelope whose BIP digest could match that grant.
  CapabilityAdmission AdmitTaskBrowserCommand(
      const AuthorizedBrowserCommand& command,
      const ActorLeaseRegistry& leases,
      const TabId& tab_id,
      base::TimeTicks now);

  // The typed task-tab sibling. The exact browser session and target document
  // remain inside the canonical intent and TaskTabActionBinding; this method
  // consumes only a grant whose digest, operation, context document and lease
  // all match that immutable action.
  CapabilityAdmission AdmitTaskTabAction(
      const core_service::mojom::TaskActionEffect& action,
      std::string_view task_id,
      const ActorLeaseRegistry& leases,
      base::TimeTicks now);

  // The typed download sibling. The immutable canonical intent binds the
  // browser session and, for start, the exact HTTPS destination. Admission is
  // one use and occurs before either DownloadManager operation is called.
  CapabilityAdmission AdmitTaskDownloadAction(
      const core_service::mojom::TaskActionEffect& action,
      std::string_view task_id,
      const ActorLeaseRegistry& leases,
      base::TimeTicks now);

  // The typed attached-store sibling (decision 0133). The grant is of class
  // ProfileStoreRead and names the store's kind by operation; the row cap and
  // the words stay inside the canonical intent and TaskStoreActionBinding.
  // One use, spent before any store is read.
  CapabilityAdmission AdmitTaskStoreAction(
      const core_service::mojom::TaskActionEffect& action,
      std::string_view task_id,
      const ActorLeaseRegistry& leases,
      base::TimeTicks now);

  // Copies the registered grant's consumption fields into a dispatcher
  // envelope. False when the capability is missing, spent, or expired.
  bool CopyRegisteredCapability(const CapabilityReference& reference,
                                CapabilityGrant& out) const;

  // Records the terminal state. Called once per admitted capability, whatever
  // the outcome. After it the capability can never be admitted again — that was
  // already true from Admit, and this only records the fact.
  void Settle(const CapabilityReference& capability_reference,
              ActionResultCode code);

  bool IsSpent(const CapabilityReference& capability_reference) const;

 private:
  enum class State : uint8_t { kRegistered, kInFlight, kSettled };

  struct Record {
    State state = State::kInFlight;
    base::TimeTicks expires_at;
    ActionResultCode terminal_code = ActionResultCode::kInternalError;
    core_service::mojom::MintedCapabilityGrantPtr grant;
  };

  // Bounded by construction: capabilities are short lived, so pruning by expiry
  // is enough and no arbitrary cap is needed. An entry is kept until its own
  // expiry so that a replay inside the validity window is caught by
  // kAlreadySpent rather than by having been forgotten.
  void PruneExpired(base::TimeTicks now);

  std::map<CapabilityReference, Record> records_;
  std::string active_profile_id_;
  uint64_t active_generation_ = 0;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_SECURITY_BROWSER_ACTION_AUTHORITY_H_
