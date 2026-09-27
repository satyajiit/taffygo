// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_H_
#define TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

class CoreStateBindingRegistry;

enum class AuthoritySubmissionStage : uint8_t {
  kNotApplicable,
  kStaged,
  kInvalid,
};

enum class AuthorityStorageBinding : uint8_t {
  kNotTracked,
  kBound,
  kInvalid,
};

enum class AuthorityStorageCompletion : uint8_t {
  kNotTracked,
  kCommitted,
  kRejected,
};

// Whether an issued consent source still names what its tab is showing. Three
// answers rather than two, because a tab between documents and a tab a person
// took to another site are not the same fact and do not deserve the same
// repair (decision 0183).
//
// `kTabHasNoDocumentOfItsOwn` is the state Chromium leaves a tab in when a
// navigation commits an error document: the tab is alive, registered and still
// the task's, and `BuildDirectObservationContext` answers nothing, so there is
// no origin to compare the source against. A phone measured it — a typed
// address whose host did not resolve logged `site=0 live=` on the refusal and
// one `at=source-not-live`, and the task never proposed an admitted move
// again. Read this as "ask again later", never as "the source is still good":
// only a refresh of a source the ledger already holds may tolerate it, and
// minting a new source against a document nobody can observe stays refused.
enum class IssuedSourceLiveness : uint8_t {
  kLive,
  kTabHasNoDocumentOfItsOwn,
  kGone,
};

// A discovery candidate is not page authority. `kAdmitted` means only that a
// future browser-to-core binding command may be sent; the source becomes
// usable after Rust durably records it and republishes the accepted binding.
enum class TaskSourceDiscoveryVerdict : uint8_t {
  kAdmitted,
  kAlreadyBound,
  kRefused,
};

// Browser-owned correlation for two distinct consent facts:
//
//   * initial source consent is standing authority for one task and source set;
//   * an in-task action approval is one-use input to policy-engine.
//
// A submitted command grants nothing. The exact browser storage commit and a
// later browser-acknowledged Rust state projection must agree before either
// fact is usable. This closes the ordering gap between independent Mojo pipes
// without treating an admission reply as durable evidence.
class AcceptedApprovalLedger final {
 public:
  AcceptedApprovalLedger();
  AcceptedApprovalLedger(const AcceptedApprovalLedger&) = delete;
  AcceptedApprovalLedger& operator=(const AcceptedApprovalLedger&) = delete;
  ~AcceptedApprovalLedger();

  AuthoritySubmissionStage StageSubmittedCommand(
      const core_service::mojom::CoreServiceCommand& command,
      const std::string& browser_profile_id,
      const std::string& browser_session_id,
      const CoreStateBindingRegistry& bindings,
      uint64_t now_monotonic_ms,
      uint64_t now_utc_ms);
  // Which clause of the call above refused, when it answered `kInvalid`.
  //
  // This gate is the last one a start passes and the only one that answered
  // nothing at all: seventeen rules, one verdict, and a phone that said
  // "Taffy could not read this request" with no line anywhere. The label is
  // compiled in, names the rule and never a value from the command. It is
  // rewritten on every call, so it always belongs to the most recent one.
  const char* last_stage_refusal() const { return last_stage_refusal_; }
  // A staged operation may emit only its exact, unexpired storage commit.
  // Other operation identities are not tracked by this ledger.
  AuthorityStorageBinding BindStorageCommit(
      const core_service::mojom::EffectEnvelope& effect,
      uint64_t now_monotonic_ms);
  AuthorityStorageCompletion RecordStorageCompletion(
      const core_service::mojom::EffectResult& result);
  void RecordAdmission(const std::string& operation_id,
                       core_service::mojom::AdmissionStatus status);
  void Reconcile(const CoreStateBindingRegistry& bindings,
                 uint64_t service_generation,
                 uint64_t now_monotonic_ms);
  void RehydrateDurableAuthority(
      const core_service::mojom::CoreStateBrowserBindings& bindings,
      const CoreStateBindingRegistry& validated_bindings,
      uint64_t service_generation,
      const std::string& browser_session_id,
      uint64_t now_monotonic_ms,
      uint64_t now_utc_ms,
      base::RepeatingCallback<IssuedSourceLiveness(
          const core_service::mojom::TaskConsentSource&)> live_source);

  bool IsTaskSourceAuthorized(
      const core_service::mojom::TaskPolicyEffect& effect,
      const std::string& live_normalized_origin,
      uint64_t service_generation) const;
  bool IsTaskSourceAuthorized(
      const std::string& task_id,
      const std::string& tab_id,
      const core_service::mojom::OperationEnvelope& operation,
      const std::string& live_normalized_origin,
      uint64_t service_generation) const;

  // The same authority as `IsTaskSourceAuthorized` without its one clause
  // about the document's current origin, for the moves that leave the
  // document rather than read it: navigate, search, open a tab. Where such a
  // move may go is decided by policy from its destination and counted against
  // the sites budget (decision 0106 section 2); the origin it is leaving
  // authorizes nothing about it. Requiring the origin here deadlocked any
  // task whose page moved under it — a redirect the task did not propose
  // leaves the tab on an origin the ledger has no source for, every read is
  // then refused, correctly, and so was the only move that could have left.
  bool IsTaskTabAuthorized(
      const core_service::mojom::TaskPolicyEffect& effect,
      uint64_t service_generation) const;
  bool IsTaskTabAuthorized(
      const std::string& task_id,
      const std::string& tab_id,
      const core_service::mojom::OperationEnvelope& operation,
      uint64_t service_generation) const;

  // Display membership is the exact committed consent record, never a grant.
  core_service::mojom::TaskConsentSourcePtr FindTaskSourceForDisplay(
      const std::string& task_id,
      const std::string& tab_id,
      uint64_t current_task_revision,
      uint64_t service_generation) const;

  // Exact zero-source bootstrap authority. It authorizes only asking the
  // browser to create a task-owned discovery context; it never authorizes a
  // page read/query/action and disappears as soon as Rust publishes a source.
  bool HasTaskSourceDiscoveryBootstrapAuthority(
      const std::string& task_id,
      const core_service::mojom::OperationEnvelope& operation,
      uint64_t service_generation,
      const std::string& browser_session_id,
      uint32_t remaining_new_source_cap) const;

  // Validates a browser-issued live source against the accepted remaining
  // discovery cap. This method deliberately does not mutate `sources_by_tab`:
  // browser-only state must not outrun the durable reducer transition. A
  // replacement retains inert proof of this exact completed action until a
  // newer durable publication consumes it; pause or generation loss drops it.
  TaskSourceDiscoveryVerdict ValidateDiscoveredTaskSource(
      const std::string& task_id,
      const core_service::mojom::OperationEnvelope& operation,
      const core_service::mojom::TaskConsentSource& source,
      uint64_t service_generation,
      base::RepeatingCallback<
          bool(const core_service::mojom::TaskConsentSource&)> live_source);

  // Why `IsTaskTabAuthorized` said no, as the four facts that tell the four
  // repairs apart: a task with no consent record at all, a record from another
  // core generation, an effect minted before the consent it is judged against,
  // and a tab the task holds no source for. Four different things reach the
  // model as one `EGRESS_NOT_AUTHORIZED`, and reading them off a phone took a
  // rebuild until this existed.
  struct TaskTabAuthorityReason {
    bool has_record = false;
    bool generation_matches = false;
    bool revision_is_current = false;
    bool tab_is_held = false;
  };
  TaskTabAuthorityReason DescribeTaskTabAuthority(
      const core_service::mojom::TaskPolicyEffect& effect,
      uint64_t service_generation) const;

  // The consented tab for `task_id` when the task has exactly one live source.
  // Empty when there is none, or more than one — a handover names one
  // `lease_before`, so a multi-tab wait is not this surface.
  std::optional<std::string> FindSingleConsentedTab(
      const std::string& task_id,
      uint64_t service_generation) const;

  // Approval-bearing effects must consume one exact durably committed record.
  // An effect without an approval is accepted only when no record for the same
  // action is staged or committed; policy-engine still decides whether that
  // action class requires approval.
  bool ConsumeExactApproval(const core_service::mojom::TaskPolicyEffect& effect,
                            uint64_t service_generation,
                            const std::string& browser_session_id,
                            uint64_t now_monotonic_ms,
                            uint64_t now_utc_ms);

  void RevokeAction(const std::string& task_id, const std::string& action_id);
  void RevokeTaskApprovals(const std::string& task_id,
                           uint64_t service_generation);
  void RevokeTask(const std::string& task_id, uint64_t service_generation);
  // Withdraws live authority while retaining only browser-produced records
  // whose exact storage transaction completed. The candidates are inert until
  // one successor generation projects the same durable facts.
  void ResetForServiceGenerationLoss();
  void Reset();

  size_t staged_approval_count_for_testing() const {
    return staged_approvals_.size();
  }
  size_t committed_approval_count_for_testing() const {
    return committed_approvals_.size();
  }
  size_t staged_consent_count_for_testing() const {
    return staged_consents_.size();
  }
  size_t accepted_consent_count_for_testing() const {
    return accepted_consents_.size();
  }
  size_t suspended_consent_count_for_testing() const {
    return suspended_consents_.size();
  }
  size_t staged_resume_count_for_testing() const {
    size_t count = 0u;
    for (const auto& entry : suspended_consents_) {
      const SuspendedConsentRecord& record = entry.second;
      count += static_cast<size_t>(record.resume_submission.has_value());
    }
    return count;
  }

 private:
  using ApprovalKey = std::pair<std::string, std::string>;

  struct SubmissionRecord {
    std::string operation_id;
    std::string idempotency_key;
    std::string task_id;
    uint64_t service_generation = 0;
    uint64_t submitted_revision = 0;
    uint64_t deadline_monotonic_ms = 0;
    uint64_t resulting_revision = 0;
    bool storage_bound = false;
    bool storage_committed = false;
  };

  struct ApprovalRecord {
    SubmissionRecord submission;
    std::string action_id;
    std::string receipt_reference;
    std::string proposal_digest;
    uint64_t expires_at_monotonic_ms = 0;
    uint64_t expires_at_utc_ms = 0;
    std::string browser_session_id;
  };

  struct ConsentSourceRecord {
    std::string source_id;
    std::string normalized_origin;
    std::optional<std::string> canonical_locator;
  };

  struct SourceReplacementRecord {
    ConsentSourceRecord source;
    std::string previous_source_id;
    std::string operation_id;
    uint64_t service_generation = 0u;
    uint64_t submitted_revision = 0u;
  };

  struct ConsentRecord {
    SubmissionRecord submission;
    std::string receipt_reference;
    std::string browser_session_id;
    bool source_discovery_enabled = false;
    uint32_t remaining_new_source_cap = 0u;
    core_service::mojom::TaskProviderRoute provider_route =
        core_service::mojom::TaskProviderRoute::kNotConfigured;
    std::vector<std::string> source_order;
    base::flat_map<std::string, ConsentSourceRecord> sources_by_tab;
    base::flat_map<std::string, SourceReplacementRecord> pending_replacements;
  };

  // A pause withdraws standing page authority without destroying the exact
  // browser-owned consent that may be restored by a later user resume. The
  // retained consent is inert. `resume_submission` is a second, independently
  // storage-bound browser operation; a core projection cannot create it.
  struct SuspendedConsentRecord {
    ConsentRecord consent;
    std::optional<SubmissionRecord> resume_submission;
  };

  SubmissionRecord* FindStagedSubmission(const std::string& operation_id);
  void EraseStagedSubmission(const std::string& operation_id);
  AuthoritySubmissionStage StageStartCommand(
      SubmissionRecord submission,
      const core_service::mojom::StartTaskCommand& start,
      const core_service::mojom::OperationEnvelope& operation,
      const std::string& browser_profile_id,
      const std::string& browser_session_id,
      const CoreStateBindingRegistry& bindings);
  AuthoritySubmissionStage StageResumeCommand(
      SubmissionRecord submission,
      const core_service::mojom::ResumeTaskCommand& resume,
      const std::string& browser_session_id,
      const CoreStateBindingRegistry& bindings,
      uint64_t now_monotonic_ms);
  void ApplyTaskSettlement(const std::string& task_id,
                           uint64_t service_generation,
                           core_service::mojom::TaskSettlementKind kind);
  bool ReceiptIsUnique(const std::string& receipt_reference) const;
  bool OperationIsUnique(const std::string& operation_id) const;
  std::optional<ConsentRecord> RehydrateConsentSources(
      const ConsentRecord& candidate,
      const core_service::mojom::AcceptedTaskConsentBinding& binding,
      base::RepeatingCallback<IssuedSourceLiveness(
          const core_service::mojom::TaskConsentSource&)> live_source) const;

  base::flat_map<ApprovalKey, ApprovalRecord> staged_approvals_;
  base::flat_map<ApprovalKey, ApprovalRecord> committed_approvals_;
  base::flat_map<std::string, ConsentRecord> staged_consents_;
  base::flat_map<std::string, ConsentRecord> accepted_consents_;
  base::flat_map<ApprovalKey, ApprovalRecord>
      generation_loss_approval_candidates_;
  base::flat_map<std::string, ConsentRecord>
      generation_loss_consent_candidates_;
  base::flat_map<std::string, SuspendedConsentRecord> suspended_consents_;
  const char* last_stage_refusal_ = "";
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ACCEPTED_APPROVAL_LEDGER_H_
