// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_H_
#define TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/field_challenge_capture.h"
#include "taffy/browser/field_values/field_value_surface.mojom.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/components/security/browser/value_reference_vault.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy {
class CoreServiceManager;
}  // namespace taffy

// The browser's half of decision 0088: the model names a form, the browser
// decides which of its fields need a person, a surface the browser owns asks
// them, and what crosses back into the isolated core is a count.
//
// # What this owns, and what it refuses to own
//
// It owns the open ask, the description the surface draws from, the re-read
// of each target field at the moment the answer arrives, the mint, and the
// count. It owns nothing about a value: there is no member here that holds
// one past the statement that moves it into the vault, no accessor that
// returns one, and no path from any of them to the status plane, the journal
// or a model request.
//
// # Why the browser decides which fields need a person
//
// The browser is already the authority on a field's classification: it
// re-reads it from the node and believes nothing a proposal asserted. A model
// that could enumerate the fields needing a person could also enumerate the
// ones that do not, and a field's class would then be decided in two places
// that disagree exactly when it matters (decision 0088 section 1).
//
// # The name of a held value is derived, and that is what makes a count
// # sufficient
//
// The core is told a number. It later composes a fill proposal naming the
// person's n-th answer, and that has to resolve against the vault without
// either side ever having sent the other a name. Both derive it from the
// request identity and the answer's position:
// `value_reference_for_supplied` in
// taffy-core/components/intelligence/core/rust/task-engine/src/field_values.rs
// is the other half, and `DerivedValueReference` below must agree with it
// exactly.
//
// # Bounded form and challenge coverage
//
//   * A form-region re-read carries the exact bounded child identities emitted
//     by the form adapter. The browser resolves every child independently and
//     opens the request only after the whole bounded set is known. More than
//     the surface/vault limit refuses whole and falls through to handover;
//     partial forms are never drawn.
//   * **Challenge classification is a hint only.** The renderer supplies the
//     closed structural kind now, and this coordinator projects it without
//     widening policy, issuing a capability, or removing the handover floor.
//   * Image pixels are copied by the trusted browser from fresh renderer
//     geometry, clipped and bounded, then the target is re-resolved before the
//     bytes reach the person-facing surface. Interactive widgets carry a
//     normalized highlight from the same browser-only path. Neither result has
//     a route to the core, model, journal, or status plane.
//   * **One-time codes and challenge responses are person-supplied per use.**
//     They have distinct sensitivity members and the vault can construct a
//     clearance for exactly those two; the other credential classes still
//     have no constructible clearance.
//
// UI thread only.

namespace taffy {

// How long the browser holds a value a person typed.
//
// Two minutes, and the number's argument is decision 0088 section 4's: the
// classes a person supplies per use exist because they read something and
// type it back within a minute or two. A longer window buys nothing an errand
// uses and widens the only window in which these bytes are in memory at all.
inline constexpr base::TimeDelta kFieldValueLifetime = base::Minutes(2);
inline constexpr uint32_t kFieldValueApprovalLifetimeSeconds = 120u;
static_assert(kFieldValueLifetime ==
              base::Seconds(kFieldValueApprovalLifetimeSeconds));

// Browser-owned identity of the live document behind one task tab. Only
// `host` is shown; the remaining fields bind a confirmation to the exact page
// and never cross into Core Service or Core API.
struct FieldValueDocument {
  std::string host;
  std::string normalized_origin;
  std::string frame_id;
  std::string page_epoch;
  uint64_t graph_revision = 0u;

  bool is_well_formed() const;
};

// The only facts returned when the browser spends a visible form
// confirmation. They are the exact deadline the person saw, never a fresh
// window minted later by the approval machinery.
struct FormFillPreapprovalReceipt {
  uint64_t expires_at_monotonic_ms = 0u;
  uint64_t expires_at_utc_ms = 0u;
};

// What one of the person's answers is called where the browser holds it.
//
// `{request_id}-value-{index}`, and the format is not this file's to choose:
// it is `value_reference_for_supplied` in the task engine, and a name that
// disagreed by one character would resolve to nothing at the moment a fill
// proposal named it. Total in the sense that function is total — it always
// produces a non-empty string — but the result is still checked for validity
// by the caller, because a `ValueReference` is bounded and a request identity
// is not bounded by the same number.
std::string DerivedValueReference(const std::string& request_id,
                                  uint32_t index);

// Whether a person, rather than the assistant, has to put this field's value
// in. Declared here because it is the rule a reviewer wants to find, and
// because the suite asserts on it directly.
bool FieldNeedsAPerson(const ResolvedNodeFacts& facts);

// Deliberately not a CoreServiceManager::Observer, although the manager fans
// the request out to that interface as well.
//
// An observer of the manager is a *watcher*, and the manager reads its own
// observer list as the answer to "is any Taffy surface looking at this
// profile" — an empty list is what makes an approval or permission surface
// report unavailable rather than succeed into nothing, and what lets an idle
// profile release its utility process. A coordinator that lived on that list
// for the life of the profile would answer yes to both questions forever.
// So the manager owns this object and hands it the request directly, and the
// observer notification stays what it is: how a surface that is genuinely
// watching learns a sheet is open.
class FieldValueRequestCoordinator final
    : public browser::field_values::mojom::TaffyFieldValueSurface {
 public:
  using NodeFactsCallback =
      base::OnceCallback<void(std::optional<ResolvedNodeFacts>)>;

  // Answers what one node of one tab in this profile is, re-read now. A seam
  // rather than a direct call so that a host test can drive this class
  // without a WebContents; production binds it to the profile's page
  // intelligence hosts.
  using NodeFactsResolver =
      base::RepeatingCallback<void(const std::string& tab_id,
                                   const std::string& node_id,
                                   NodeFactsCallback on_resolved)>;

  // Answers the exact live document one tab of this profile is showing. Empty
  // when the tab is gone or its origin is opaque, which the surface reads as
  // a request it must not draw.
  using TabDocumentResolver =
      base::RepeatingCallback<std::optional<FieldValueDocument>(
          const std::string& tab_id)>;

  // Reports how many values the person supplied for one request, what
  // became of the ask, and which field each held value was minted for — and
  // nothing else about any of them.
  //
  // The outcome is a closed member chosen from this object's own control flow
  // and never from anything on the page (decision 0215). A zero count reaches
  // the core for six different reasons, each wanting a different next move,
  // and this is the only party that knows which. `field_node_ids` has exactly
  // `supplied` entries in position order: the node identities the sheet's
  // rows were about, so the task can put each value into its field without a
  // model turn naming them (decision 0238).
  using SuppliedCountSink = base::RepeatingCallback<void(
      const std::string& task_id,
      const std::string& request_id,
      uint32_t supplied,
      core_service::mojom::FieldValueAskOutcome outcome,
      const std::vector<std::string>& field_node_ids)>;

  // Releases the manager's one-shot emission identity when an ask reaches a
  // terminal path. The callback carries no answer or page content.
  using RequestClosedSink =
      base::RepeatingCallback<void(const std::string& request_id)>;

  using ChallengePresentationResolver =
      base::RepeatingCallback<base::OnceClosure(
          const std::string& tab_id,
          const std::string& node_id,
          ResolvedNodeFacts facts,
          FieldChallengePresentationCallback completion)>;

  // The production wiring: the profile's vault, its page intelligence hosts,
  // and its command submission. `manager` owns the returned object.
  static std::unique_ptr<FieldValueRequestCoordinator> ForProfile(
      CoreServiceManager* manager,
      content::BrowserContext* browser_context);

  FieldValueRequestCoordinator(ValueReferenceVault* vault,
                               NodeFactsResolver resolve_node,
                               TabDocumentResolver resolve_document,
                               SuppliedCountSink report_supplied);
  FieldValueRequestCoordinator(
      ValueReferenceVault* vault,
      NodeFactsResolver resolve_node,
      TabDocumentResolver resolve_document,
      SuppliedCountSink report_supplied,
      ChallengePresentationResolver resolve_challenge_presentation,
      RequestClosedSink request_closed = RequestClosedSink());
  FieldValueRequestCoordinator(const FieldValueRequestCoordinator&) = delete;
  FieldValueRequestCoordinator& operator=(const FieldValueRequestCoordinator&) =
      delete;
  ~FieldValueRequestCoordinator() override;

  // The entry point a platform bridge calls to hand the surface over.
  // Android connects the profile-owned Compose endpoint through this pipe.
  // A non-product build connects nothing, so every request is answered with
  // zero and falls through to the handover floor from decision 0088 section 3.
  void Bind(mojo::PendingReceiver<
            browser::field_values::mojom::TaffyFieldValueSurface> receiver);

  // Closes every open request. Called when the profile generation goes: the
  // values it held are gone with it, so a sheet still collecting more of them
  // would be collecting into a vault that can no longer hold them.
  void CloseAllRequests();
  void CloseRequestsForTask(const std::string& task_id);

  size_t open_request_count() const { return open_.size(); }
  size_t preapproval_count_for_testing() const { return preapprovals_.size(); }

  // Spends the next exact field in the one-use authorization created by a
  // successful Supply. Every non-value fact is compared here; the value bytes
  // stay in ValueReferenceVault and are neither read nor derived from.
  std::optional<FormFillPreapprovalReceipt> ConsumeFormFillPreapproval(
      const std::string& task_id,
      const std::string& tab_id,
      const std::string& node_id,
      const std::string& request_id,
      uint32_t index,
      const std::string& normalized_origin,
      const std::string& frame_id,
      const std::string& page_epoch,
      uint64_t graph_revision,
      uint64_t now_monotonic_ms,
      uint64_t now_utc_ms);

  // Whether ConsumeFormFillPreapproval would spend a receipt for exactly
  // these facts now. It spends and invalidates nothing. A fill's first policy
  // ask reads it, so a fill of a value the person never gave is refused before
  // any question is published (decision 0239).
  bool HoldsFormFillPreapproval(const std::string& task_id,
                                const std::string& tab_id,
                                const std::string& node_id,
                                const std::string& request_id,
                                uint32_t index,
                                const std::string& normalized_origin,
                                const std::string& frame_id,
                                const std::string& page_epoch,
                                uint64_t graph_revision,
                                uint64_t now_monotonic_ms,
                                uint64_t now_utc_ms) const;

  // One open ask, handed over by the manager's surface arm. Named for the
  // observer method it is driven from, because they carry the same four facts
  // and a reader following one should land on the other.
  //
  // `companion_node_ids` are the other fields on the page that only the
  // person can supply, which the core asks about beside a field that is in no
  // form (decision 0238). Each is re-read like the named one and asked about
  // only if it still needs a person; one that cannot be shown is left off the
  // sheet rather than costing the person the rows that can.
  void OnCoreFieldValueRequest(
      const std::string& request_id,
      const std::string& task_id,
      const std::string& tab_id,
      const std::string& node_id,
      const std::vector<std::string>& companion_node_ids = {});

  // browser::field_values::mojom::TaffyFieldValueSurface:
  void Connect(
      mojo::PendingRemote<browser::field_values::mojom::TaffyFieldValueClient>
          client) override;
  void Supply(const std::string& request_id,
              const std::vector<std::string>& values,
              SupplyCallback callback) override;
  void Dismiss(const std::string& request_id) override;

 private:
  using CloseReason = browser::field_values::mojom::FieldValueCloseReason;

  // One row of an open request: the node it is about and nothing read off it.
  // The classification is deliberately *not* cached here — it is re-read when
  // the answer arrives, because the page may have changed while the person
  // typed and the class at that moment is the one that decides.
  struct RequestedField {
    std::string field_id;
    std::string node_id;
  };

  struct OpenRequest {
    std::string task_id;
    std::string tab_id;
    // The node the task named, and the companions it asked about beside it.
    // A gap on the named field ends the request, as it always has; a gap on
    // a companion only drops that row.
    std::string named_node_id;
    std::vector<std::string> companion_node_ids;
    FieldValueDocument document;
    std::vector<std::string> candidate_node_ids;
    size_t next_candidate = 0;
    std::vector<RequestedField> fields;
    std::vector<browser::field_values::mojom::FieldValueDescriptorPtr>
        descriptors;
    bool awaiting_challenge_presentation = false;
    base::OnceClosure cancel_challenge_presentation;
  };

  // One answer being settled: the values, the request it belongs to, the
  // reply the surface is waiting on, and how far through the fields it is.
  struct PendingAnswer {
    std::string request_id;
    OpenRequest request;
    std::vector<std::string> values;
    std::vector<browser::field_values::mojom::FieldValueRefusalPtr> refused;
    std::vector<ValueReference> minted_references;
    uint32_t index = 0;
    uint32_t minted = 0;
    base::TimeTicks expires_at;
    base::Time expires_at_utc;
    // A count can name only a contiguous prefix of derived references. Once
    // one position cannot be held, later positions are scrubbed and refused
    // rather than minted across a gap the isolated core has no vocabulary to
    // describe.
    bool prefix_closed = false;
    SupplyCallback callback;
  };

  // One explicit "Approve once" over the exact visible sequence. It contains
  // no value, length, digest, prefix, or other value-derived material.
  struct FormFillPreapproval {
    std::string task_id;
    std::string tab_id;
    std::string normalized_origin;
    std::string frame_id;
    std::string page_epoch;
    uint64_t graph_revision = 0u;
    std::vector<RequestedField> fields;
    uint32_t next_index = 0u;
    uint64_t expires_at_monotonic_ms = 0u;
    uint64_t expires_at_utc_ms = 0u;
  };

  // The one exactness rule both preapproval readers apply.
  static bool IsExactPreapprovalUse(const FormFillPreapproval& approval,
                                    const std::string& task_id,
                                    const std::string& tab_id,
                                    const std::string& node_id,
                                    uint32_t index,
                                    const std::string& normalized_origin,
                                    const std::string& frame_id,
                                    const std::string& page_epoch,
                                    uint64_t graph_revision,
                                    uint64_t now_monotonic_ms,
                                    uint64_t now_utc_ms);

  void OnFormResolved(const std::string& request_id,
                      const std::string& node_id,
                      std::optional<ResolvedNodeFacts> facts);
  void ResolveNextCandidate(const std::string& request_id);
  void OnCandidateResolved(const std::string& request_id,
                           const std::string& node_id,
                           std::optional<ResolvedNodeFacts> facts);
  void OnChallengePresentation(
      const std::string& request_id,
      ResolvedNodeFacts facts,
      std::optional<FieldChallengePresentation> presentation,
      const char* capture_refused_at);
  void OpenPreparedRequest(const std::string& request_id);
  // Walks `answer` one field at a time. Sequential rather than parallel: the
  // count is a report of what was minted, so it cannot be composed until
  // every mint has either happened or been refused, and eight round trips in
  // order is easier to be sure of than eight in flight.
  void SettleNextField(std::unique_ptr<PendingAnswer> answer);
  void OnTargetResolved(std::unique_ptr<PendingAnswer> answer,
                        std::optional<ResolvedNodeFacts> facts);
  void FinishAnswer(std::unique_ptr<PendingAnswer> answer);
  void RecordPreapproval(const PendingAnswer& answer);

  // Drops the request and tells the surface, exactly once.
  void CloseRequest(const std::string& request_id, CloseReason reason);
  // Closes every open request. `report_zero` decides whether the tasks are
  // told they were answered with nothing, and the two callers differ on it
  // for a reason each of them states.
  void CloseEveryRequest(CloseReason reason, bool report_zero);
  // The request produced nothing to ask about, so the task is told the person
  // supplied nothing rather than left waiting on a sheet nobody will draw.
  // `at` names the clause that gave up, as a compiled-in word for the log.
  //
  // The count is reported *before* the request is closed. The manager admits
  // a count only for a request it still holds as emitted, and closing is what
  // tells it to forget the request, so the other order had every abandoned
  // request's answer refused as `at=submit/revision`, leaving the errand
  // waiting on a sheet that was never drawn (verification report,
  // section 2.48).
  //
  // `outcome` is passed per call site rather than derived from `at` by a
  // table, so adding a clause is a decision about what the task should do
  // next rather than a lookup that silently misses (decision 0215). The two
  // are different granularities on purpose: `at` stays at fourteen clauses
  // for a person reading a device log, and the outcome is the instruction.
  void AbandonRequest(const std::string& request_id,
                      const std::string& task_id,
                      CloseReason reason,
                      const char* at,
                      core_service::mojom::FieldValueAskOutcome outcome);
  void NotifyRequestClosed(const std::string& request_id);

  const raw_ptr<ValueReferenceVault> vault_;
  const NodeFactsResolver resolve_node_;
  const TabDocumentResolver resolve_document_;
  const SuppliedCountSink report_supplied_;
  const ChallengePresentationResolver resolve_challenge_presentation_;
  const RequestClosedSink request_closed_;

  std::map<std::string, OpenRequest> open_;
  std::map<std::string, FormFillPreapproval> preapprovals_;

  mojo::ReceiverSet<browser::field_values::mojom::TaffyFieldValueSurface>
      receivers_;
  mojo::Remote<browser::field_values::mojom::TaffyFieldValueClient> client_;

  base::WeakPtrFactory<FieldValueRequestCoordinator> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_H_
