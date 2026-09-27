// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_page_observation_broker.h"

#include <stdint.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/page_inspector_projection_mapper.h"
#include "taffy/browser/page_media_observation.h"
#include "taffy/browser/page_screenshot_fallback.h"
#include "taffy/browser/profile_page_media_store.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

service_mojom::BipObservationStatus ToWireStatus(ObservationResultCode status) {
  switch (status) {
    case ObservationResultCode::kOk:
      return service_mojom::BipObservationStatus::kOk;
    case ObservationResultCode::kUnsupported:
      return service_mojom::BipObservationStatus::kUnsupported;
    case ObservationResultCode::kIncomplete:
      return service_mojom::BipObservationStatus::kIncomplete;
    case ObservationResultCode::kConflicted:
      return service_mojom::BipObservationStatus::kConflicted;
    case ObservationResultCode::kStalePageEpoch:
      return service_mojom::BipObservationStatus::kStalePageEpoch;
    case ObservationResultCode::kDocumentInactive:
      return service_mojom::BipObservationStatus::kDocumentInactive;
    case ObservationResultCode::kBudgetExceeded:
      return service_mojom::BipObservationStatus::kBudgetExceeded;
    case ObservationResultCode::kDeadlineExceeded:
      return service_mojom::BipObservationStatus::kDeadlineExceeded;
    case ObservationResultCode::kCancelled:
      return service_mojom::BipObservationStatus::kCancelled;
    case ObservationResultCode::kResourcePressure:
      return service_mojom::BipObservationStatus::kResourcePressure;
    case ObservationResultCode::kInternalError:
      return service_mojom::BipObservationStatus::kInternalError;
  }
}

ActionResultCode ToLedgerResult(ObservationResultCode status) {
  switch (status) {
    // The three codes that mean the reading happened. `kConflicted` is a
    // caveat on a graph that arrived, not a graph that did not: the renderer
    // attaches a snapshot for it by contract (`SnapshotBuilder::Output`), and
    // the two other places that state this rule — `ObservationResultBuilder`
    // and `ObservedLinkRegistry::Replace` — have admitted all three since the
    // link table was fixed. This was the third copy and the one that
    // disagreed (decision 0207).
    case ObservationResultCode::kOk:
    case ObservationResultCode::kIncomplete:
    case ObservationResultCode::kConflicted:
      return ActionResultCode::kVerified;
    case ObservationResultCode::kUnsupported:
      return ActionResultCode::kUnsupported;
    case ObservationResultCode::kStalePageEpoch:
      return ActionResultCode::kStalePageEpoch;
    case ObservationResultCode::kDocumentInactive:
      return ActionResultCode::kDocumentInactive;
    // A deadline is a budget. `BudgetKind::kDeadline` is a member of the same
    // vocabulary as nodes and bytes, and running out of time says exactly what
    // running out of nodes says: the page cost more than this observation was
    // allowed to spend, nothing was read, and the answer is to ask for less.
    // Calling it an internal error said two untrue things at once - that the
    // product had a defect, and, through `Recovery::Abandon`, that the task had
    // no move left. On a phone it ended an errand that had just reached the
    // site it was looking for: the search, the reads, the query and the
    // navigation all verified, the first read of the site verified, and the
    // second one came back "internal error" because a single-page application
    // was still hydrating (decision 0171).
    case ObservationResultCode::kBudgetExceeded:
    case ObservationResultCode::kResourcePressure:
    case ObservationResultCode::kDeadlineExceeded:
      return ActionResultCode::kBudgetExceeded;
    case ObservationResultCode::kCancelled:
      return ActionResultCode::kCancelledByUser;
    case ObservationResultCode::kInternalError:
      return ActionResultCode::kInternalError;
  }
}

service_mojom::BipGraphEncoding ToWireEncoding(GraphPayloadEncoding encoding) {
  switch (encoding) {
    case GraphPayloadEncoding::kNone:
      return service_mojom::BipGraphEncoding::kNone;
    case GraphPayloadEncoding::kBipContract:
      return service_mojom::BipGraphEncoding::kBipContract;
  }
}

service_mojom::BipSensitivity ToWireSensitivity(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kNotSensitive:
      return service_mojom::BipSensitivity::kNotSensitive;
    case Sensitivity::kPersonal:
      return service_mojom::BipSensitivity::kPersonal;
    case Sensitivity::kAccount:
      return service_mojom::BipSensitivity::kAccount;
    case Sensitivity::kPayment:
      return service_mojom::BipSensitivity::kPayment;
    case Sensitivity::kIdentity:
      return service_mojom::BipSensitivity::kIdentity;
    case Sensitivity::kHealth:
      return service_mojom::BipSensitivity::kHealth;
    case Sensitivity::kFinancial:
      return service_mojom::BipSensitivity::kFinancial;
    case Sensitivity::kLegal:
      return service_mojom::BipSensitivity::kLegal;
    case Sensitivity::kPrivateCommunication:
      return service_mojom::BipSensitivity::kPrivateCommunication;
    case Sensitivity::kAdministration:
      return service_mojom::BipSensitivity::kAdministration;
    case Sensitivity::kCredential:
      return service_mojom::BipSensitivity::kCredential;
    case Sensitivity::kUnknownSensitive:
      return service_mojom::BipSensitivity::kUnknownSensitive;
    case Sensitivity::kOneTimeCode:
      return service_mojom::BipSensitivity::kOneTimeCode;
    case Sensitivity::kChallengeResponse:
      return service_mojom::BipSensitivity::kChallengeResponse;
  }
}

service_mojom::EffectStatus ToEffectStatus(ObservationResultCode status) {
  switch (status) {
    case ObservationResultCode::kOk:
    case ObservationResultCode::kIncomplete:
    case ObservationResultCode::kConflicted:
      return service_mojom::EffectStatus::kCompleted;
    case ObservationResultCode::kDeadlineExceeded:
      return service_mojom::EffectStatus::kDeadlineExceeded;
    case ObservationResultCode::kCancelled:
      return service_mojom::EffectStatus::kCancelled;
    case ObservationResultCode::kBudgetExceeded:
    case ObservationResultCode::kResourcePressure:
      return service_mojom::EffectStatus::kResourceLimit;
    case ObservationResultCode::kUnsupported:
    case ObservationResultCode::kStalePageEpoch:
    case ObservationResultCode::kDocumentInactive:
      return service_mojom::EffectStatus::kDenied;
    case ObservationResultCode::kInternalError:
      return service_mojom::EffectStatus::kUnavailable;
  }
}

bool MatchesRequest(const ObservationEnvelope& observation,
                    const service_mojom::PageObservationEffect& requested,
                    const AuthorizedObservationTarget& authorized_target) {
  if (!authorized_target.is_valid() ||
      observation.scope != authorized_target.scope ||
      observation.form_root != authorized_target.form_root ||
      observation.media_root != authorized_target.media_root) {
    return false;
  }
  return observation.tab_id.value == requested.tab_id &&
         observation.root_frame_id.value == requested.frame_id &&
         observation.page_epoch.value == requested.page_epoch &&
         observation.graph_revision >= requested.expected_graph_revision &&
         observation.node_count <= requested.max_nodes &&
         observation.frames.size() <= requested.max_frames &&
         observation.total_bytes <= requested.max_bytes &&
         observation.graph_payload.size() <= requested.max_bytes &&
         observation.graph_payload.size() <= service_mojom::kMaxEffectBytes;
}

// A reading that happened, whatever caveat it carries.
//
// The three admitted codes are the ones the renderer attaches a snapshot to.
// `kConflicted` is the page disagreeing with itself — a site whose JSON-LD
// says something its own text does not — and the structured-data adapter is
// optional on every scope that requests it. Refusing the whole reading for it
// threw away a complete graph and told the model `DENIED_BY_POLICY`, whose
// recovery is `DoNotRetry`: the one answer that means "asking again will not
// help". On a phone it ended an errand that had walked to the site it wanted
// and read 922 nodes of it (decision 0207).
bool IsSuccessfulObservation(ObservationResultCode code) {
  return code == ObservationResultCode::kOk ||
         code == ObservationResultCode::kIncomplete ||
         code == ObservationResultCode::kConflicted;
}

std::optional<PageMediaObservationKind> MediaKindFor(
    AuthorizedObservationKind kind) {
  switch (kind) {
    case AuthorizedObservationKind::kImageDescription:
    case AuthorizedObservationKind::kImageText:
      return PageMediaObservationKind::kImage;
    case AuthorizedObservationKind::kVideo:
      return PageMediaObservationKind::kVideo;
    case AuthorizedObservationKind::kPdf:
      return PageMediaObservationKind::kPdf;
    case AuthorizedObservationKind::kPageScreenshot:
      return PageMediaObservationKind::kPageScreenshot;
    case AuthorizedObservationKind::kDocument:
    case AuthorizedObservationKind::kForm:
    case AuthorizedObservationKind::kSelection:
      return std::nullopt;
  }
}

}  // namespace

namespace core_mojom = core_service::mojom;

void CorePageObservationBroker::OnObservation(
    core_mojom::EffectEnvelopePtr effect,
    CompletionCallback callback,
    bool create_direct_projection,
    std::optional<ObservationEnvelope> observation) {
  std::optional<AuthorizedObservationTarget> authorized_target;
  if (effect) {
    auto pending = pending_observations_.find(effect->effect_id);
    if (pending != pending_observations_.end()) {
      authorized_target = pending->second.authorized_target;
      pending_observations_.erase(pending);
    }
  }
  if (!effect || !effect->page_observation || !authorized_target ||
      !observation) {
    CoreServiceManager* manager = LookupManager();
    if (manager && effect && effect->page_observation) {
      manager->capabilities()->Settle(
          CapabilityReference{effect->page_observation->capability_id},
          ActionResultCode::kInternalError);
    }
    std::move(callback).Run(effect ? MakeUnavailable(*effect) : nullptr);
    return;
  }

  ObservationEnvelope value = std::move(*observation);
  const core_mojom::PageObservationEffect& requested =
      *effect->page_observation;
  const bool matches_request =
      MatchesRequest(value, requested, *authorized_target);
  const std::optional<PageMediaObservationKind> media_kind =
      MediaKindFor(authorized_target->kind);
  const bool screenshot_eligible =
      !media_kind || *media_kind != PageMediaObservationKind::kPageScreenshot ||
      PageScreenshotFallbackIsEligible(value);
  if (!media_kind || !matches_request ||
      !IsSuccessfulObservation(value.code) || !screenshot_eligible) {
    FinishObservation(std::move(effect), std::move(callback),
                      create_direct_projection, std::move(*authorized_target),
                      std::move(value), nullptr);
    return;
  }

  const std::string effect_id = effect->effect_id;
  const bool inserted =
      pending_media_observations_
          .emplace(effect_id,
                   PendingMediaObservation{
                       .task_id = requested.task_id,
                       .generation = effect->operation->service_generation})
          .second;
  if (!inserted) {
    FinishObservation(std::move(effect), std::move(callback),
                      create_direct_projection, std::move(*authorized_target),
                      std::move(value), nullptr);
    return;
  }
  const uint64_t minimum_revision =
      *media_kind == PageMediaObservationKind::kPageScreenshot
          ? value.graph_revision
      : authorized_target->media_root
          ? authorized_target->media_root->minimum_graph_revision
          : requested.expected_graph_revision;
  base::OnceClosure cancel = StartPageMediaObservation(
      browser_context_,
      PageMediaObservationRequest{
          .task_id = requested.task_id,
          .effect_id = effect_id,
          .service_generation = effect->operation->service_generation,
          .tab_id = requested.tab_id,
          .frame_id = requested.frame_id,
          .page_epoch = requested.page_epoch,
          .minimum_graph_revision = minimum_revision,
          .kind = *media_kind,
          .node_id = authorized_target->media_root
                         ? std::optional<std::string>(
                               authorized_target->media_root->node_id.value)
                         : std::nullopt,
          .deadline = base::Milliseconds(requested.deadline_ms),
      },
      page_media_store_,
      base::BindOnce(&CorePageObservationBroker::OnMediaObservation,
                     weak_factory_.GetWeakPtr(), std::move(effect),
                     std::move(callback), create_direct_projection,
                     std::move(*authorized_target), std::move(value)));
  auto still_pending = pending_media_observations_.find(effect_id);
  if (still_pending != pending_media_observations_.end()) {
    still_pending->second.cancel = std::move(cancel);
  }
}

void CorePageObservationBroker::FinishObservation(
    core_mojom::EffectEnvelopePtr effect,
    CompletionCallback callback,
    bool create_direct_projection,
    AuthorizedObservationTarget authorized_target,
    ObservationEnvelope value,
    core_mojom::MediaObservationResultPtr media) {
  if (!effect || !effect->page_observation) {
    std::move(callback).Run(effect ? MakeUnavailable(*effect) : nullptr);
    return;
  }
  const core_mojom::PageObservationEffect& requested =
      *effect->page_observation;
  const bool base_matches = MatchesRequest(value, requested, authorized_target);
  const bool is_media = MediaKindFor(authorized_target.kind).has_value();
  const bool media_matches =
      is_media ? (!IsSuccessfulObservation(value.code) || !!media) : !media;
  const bool screenshot_matches =
      authorized_target.kind != AuthorizedObservationKind::kPageScreenshot ||
      (media && PageScreenshotResultMatchesObservation(value, *media));
  const bool correlates = base_matches && media_matches && screenshot_matches;
  // Correlation is a question about a reading that happened.
  //
  // An observation that produced none carries a bare envelope - no frame, no
  // epoch, no scope, no graph - so every identity clause fails, and the answer
  // became "this result does not match that request": the ledger settled it
  // `kInternalError`, whose recovery is `Abandon`, and the terminal said
  // `kInvalidResult` instead of what happened. A deadline, a spent budget, a
  // cancelled walk and a document that went inactive are none of them a defect
  // in this product, and one of them ended an errand on a phone on its third
  // move, two seconds after the page it was reading had answered perfectly
  // well twice (decision 0177). A failed reading reports its own code; only a
  // reading that produced a snapshot has anything to correlate.
  const bool produced_a_reading = IsSuccessfulObservation(value.code);
  const bool matches_request = produced_a_reading ? correlates : true;
  CoreServiceManager* manager = LookupManager();
  if (manager) {
    manager->capabilities()->Settle(
        CapabilityReference{requested.capability_id},
        matches_request ? ToLedgerResult(value.code)
                        : ActionResultCode::kInternalError);
  }

  // A direct projection is a second consumer of the same observation. It may
  // see only an envelope that passed the task/result correlation checks below;
  // otherwise a mismatched tab or stale page could reach the UI even though
  // the Core Service terminal was rejected afterward.
  if (create_direct_projection && produced_a_reading && correlates &&
      requested.authority_subject &&
      requested.authority_subject->kind ==
          core_mojom::AuthoritySubjectKind::kDirectUserIntent) {
    core_api::mojom::PageInspectorSnapshotViewPtr projection =
        ProjectPageInspectorObservation(value);
    if (projection) {
      // A live consumer claims the projection synchronously from the
      // completion callback below. Anything still here belongs to a callback
      // whose weak owner disappeared, and retaining another bounded snapshot
      // beside it would leak page text across manager generations.
      direct_projections_.clear();
      direct_projections_.insert_or_assign(effect->effect_id,
                                           std::move(projection));
    }
  }
  auto result = core_mojom::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = core_mojom::EffectKind::kPageObservation;
  result->status = ToEffectStatus(value.code);
  result->observation = core_mojom::ObservationEffectResult::New();
  result->observation->status = ToWireStatus(value.code);
  result->observation->schema_version = value.schema_version;
  result->observation->tab_id = value.tab_id.value;
  result->observation->frame_id = value.root_frame_id.value;
  result->observation->page_epoch = value.page_epoch.value;
  result->observation->graph_revision = value.graph_revision;
  result->observation->origin =
      value.origin.is_opaque() ? std::string() : value.origin.serialization;
  result->observation->is_potentially_trustworthy =
      value.is_potentially_trustworthy;
  result->observation->private_profile = value.is_incognito;
  result->observation->node_count = value.node_count;
  result->observation->total_bytes = value.total_bytes;
  result->observation->truncated = value.truncation.truncated;
  result->observation->may_change_answer = value.truncation.may_change_answer;
  result->observation->redacted_field_count =
      value.redaction.redacted_field_count;
  result->observation->suppressed_secret_value_count =
      value.redaction.suppressed_secret_value_count;
  result->observation->sensitive_zone_count =
      value.redaction.sensitive_zone_count;
  result->observation->policy_filtered_frame_count =
      value.redaction.policy_filtered_frame_count;
  result->observation->highest_sensitivity =
      ToWireSensitivity(value.redaction.highest_class_present);
  result->observation->graph_encoding = ToWireEncoding(value.encoding);

  if (!matches_request) {
    result->status = base_matches && is_media && !media
                         ? core_mojom::EffectStatus::kUnavailable
                         : core_mojom::EffectStatus::kInvalidResult;
    result->observation->graph_encoding = core_mojom::BipGraphEncoding::kNone;
  } else {
    // The envelope is consumed here. Moving avoids a second allocation and a
    // full copy of the bounded (up to one-megabyte) graph payload on every
    // successful observation.
    result->observation->graph_payload = std::move(value.graph_payload);
    result->observation->media = std::move(media);
  }
  std::move(callback).Run(std::move(result));
}

core_mojom::EffectResultPtr CorePageObservationBroker::MakeUnavailable(
    const core_mojom::EffectEnvelope& effect) const {
  auto result = core_mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = core_mojom::EffectStatus::kUnavailable;
  result->kind = core_mojom::EffectKind::kPageObservation;
  result->observation = core_mojom::ObservationEffectResult::New();
  result->observation->status =
      core_mojom::BipObservationStatus::kInternalError;
  result->observation->graph_encoding = core_mojom::BipGraphEncoding::kNone;
  result->observation->highest_sensitivity =
      core_mojom::BipSensitivity::kUnknownSensitive;
  return result;
}

}  // namespace taffy
