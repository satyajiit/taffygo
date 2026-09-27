// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

using AskOutcome = core_service::mojom::FieldValueAskOutcome;

namespace {

namespace surface_mojom = browser::field_values::mojom;

surface_mojom::FieldChallengeKind SurfaceChallenge(ChallengeKind challenge) {
  switch (challenge) {
    case ChallengeKind::kNone:
      return surface_mojom::FieldChallengeKind::kNone;
    case ChallengeKind::kImage:
      return surface_mojom::FieldChallengeKind::kImageChallenge;
    case ChallengeKind::kInteractive:
      return surface_mojom::FieldChallengeKind::kInteractiveChallenge;
    case ChallengeKind::kOneTimeCode:
      return surface_mojom::FieldChallengeKind::kOneTimeCode;
  }
}

// What the sheet would be missing for this field, as the clause an
// abandonment is logged under, or nullptr when it has all it needs: the
// picture of an image challenge, the outline of an interactive one, and a
// label for every row.
const char* PresentationGap(
    const ResolvedNodeFacts& facts,
    const std::optional<FieldChallengePresentation>& presentation) {
  if (facts.challenge_kind == ChallengeKind::kImage &&
      (!presentation || presentation->image_png.empty())) {
    return "no-challenge-picture";
  }
  if (facts.challenge_kind == ChallengeKind::kInteractive &&
      (!presentation || !presentation->highlight.has_value())) {
    return "no-challenge-highlight";
  }
  if (facts.display_label.empty()) {
    return "no-label";
  }
  return nullptr;
}

// What the task should do about a sheet this field could not be put on.
//
// Reads the *capture's* clause and not the coordinator's, because the
// coordinator's is always "no picture" and the distinction that decides the
// next move is a layer below it (decision 0215). Exactly one capture clause
// means the target was right and the page merely has to move.
//
// A null clause answers kCannotBeShown and covers two unrelated cases at
// once, which is worth knowing rather than guarding: the capture succeeded
// and the gap is the missing label, or no capture ran because this field is
// not a challenge at all. Neither can be off-screen — `PresentationGap`
// returns a picture or highlight gap before it looks at the label — so a
// branch for them would be a branch that cannot change the answer, which
// decision 0212 says not to write.
AskOutcome ChallengeOutcome(const char* capture_refused_at) {
  return capture_refused_at != nullptr &&
                 std::string_view(capture_refused_at) ==
                     kFieldChallengeRefusedOffScreen
             ? AskOutcome::kChallengeOffScreen
             : AskOutcome::kCannotBeShown;
}

bool SamePage(const FieldValueDocument& expected,
              const FieldValueDocument& current) {
  return expected.host == current.host &&
         expected.normalized_origin == current.normalized_origin &&
         expected.frame_id == current.frame_id &&
         expected.page_epoch == current.page_epoch &&
         current.graph_revision >= expected.graph_revision;
}

}  // namespace

void FieldValueRequestCoordinator::OnFormResolved(
    const std::string& request_id,
    const std::string& node_id,
    std::optional<ResolvedNodeFacts> facts) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  const std::string task_id = it->second.task_id;
  const std::optional<FieldValueDocument> document =
      resolve_document_.Run(it->second.tab_id);
  if (!facts.has_value() || facts->node_id.value != node_id) {
    AbandonRequest(request_id, task_id, CloseReason::kRevoked, "node-gone",
                   AskOutcome::kPageMoved);
    return;
  }
  if (!document || !SamePage(it->second.document, *document)) {
    AbandonRequest(request_id, task_id, CloseReason::kRevoked, "page-moved",
                   AskOutcome::kPageMoved);
    return;
  }
  it->second.document = *document;

  std::vector<std::string> candidates;
  if (!facts->form_field_node_ids.empty()) {
    if (facts->form_field_node_ids.size() > kMaxSuppliedFieldValues) {
      AbandonRequest(request_id, task_id, CloseReason::kRevoked,
                     "too-many-fields", AskOutcome::kCannotBeShown);
      return;
    }
    std::set<std::string> unique;
    for (const SemanticNodeId& child : facts->form_field_node_ids) {
      if (child.value.empty() || !unique.insert(child.value).second) {
        AbandonRequest(request_id, task_id, CloseReason::kRevoked,
                       "field-repeated", AskOutcome::kCannotBeShown);
        return;
      }
      candidates.push_back(child.value);
    }
  } else if (FieldNeedsAPerson(*facts)) {
    // A field in no form, and the page's other fields only the person can
    // supply beside it, so one sheet asks for an identity number and the
    // CAPTCHA under it rather than one at a time (decision 0238). Every
    // companion is re-read below like any row and left out unless it still
    // needs a person; past the sheet's rows they are not asked about at all.
    candidates.push_back(node_id);
    std::set<std::string> unique{node_id};
    for (const std::string& companion : it->second.companion_node_ids) {
      if (candidates.size() >= kMaxSuppliedFieldValues) {
        break;
      }
      if (!companion.empty() && unique.insert(companion).second) {
        candidates.push_back(companion);
      }
    }
  } else if (!it->second.companion_node_ids.empty()) {
    // A block with no fields of its own: a page that draws its fields in no
    // form element, as the myAadhaar download form does, left the model a
    // region or an unclassified block to name, and this path gave up on it
    // as "not a field" twice running while the fields were right there. The
    // task names the page's fields only the person can supply beside a
    // block, and those are what the sheet asks about. Each is re-read below
    // like any row and left out unless it still needs a person, so the block
    // widens nothing the fields themselves would not (decision 0243).
    LOG(WARNING) << "[taffy_field_request_block_expanded] companions="
                 << it->second.companion_node_ids.size();
    std::set<std::string> unique;
    for (const std::string& companion : it->second.companion_node_ids) {
      if (candidates.size() >= kMaxSuppliedFieldValues) {
        break;
      }
      if (!companion.empty() && unique.insert(companion).second) {
        candidates.push_back(companion);
      }
    }
  } else {
    AbandonRequest(request_id, task_id, CloseReason::kRevoked, "not-a-field",
                   AskOutcome::kNotAField);
    return;
  }
  it->second.candidate_node_ids = std::move(candidates);
  ResolveNextCandidate(request_id);
}

void FieldValueRequestCoordinator::ResolveNextCandidate(
    const std::string& request_id) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  if (it->second.next_candidate >= it->second.candidate_node_ids.size()) {
    OpenPreparedRequest(request_id);
    return;
  }
  const std::string node_id =
      it->second.candidate_node_ids[it->second.next_candidate++];
  const std::string tab_id = it->second.tab_id;
  resolve_node_.Run(
      tab_id, node_id,
      base::BindOnce(&FieldValueRequestCoordinator::OnCandidateResolved,
                     weak_factory_.GetWeakPtr(), request_id, node_id));
}

void FieldValueRequestCoordinator::OnCandidateResolved(
    const std::string& request_id,
    const std::string& node_id,
    std::optional<ResolvedNodeFacts> facts) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  if (!facts || facts->node_id.value != node_id || !FieldNeedsAPerson(*facts)) {
    ResolveNextCandidate(request_id);
    return;
  }

  RequestedField field;
  field.field_id = facts->node_id.value;
  field.node_id = node_id;
  it->second.fields.push_back(std::move(field));

  if (facts->challenge_kind == ChallengeKind::kImage ||
      facts->challenge_kind == ChallengeKind::kInteractive) {
    it->second.awaiting_challenge_presentation = true;
    base::OnceClosure cancel = resolve_challenge_presentation_.Run(
        it->second.tab_id, node_id, *facts,
        base::BindOnce(&FieldValueRequestCoordinator::OnChallengePresentation,
                       weak_factory_.GetWeakPtr(), request_id, *facts));
    const auto current = open_.find(request_id);
    if (current != open_.end() &&
        current->second.awaiting_challenge_presentation) {
      current->second.cancel_challenge_presentation = std::move(cancel);
    }
    return;
  }
  OnChallengePresentation(request_id, std::move(*facts),
                          FieldChallengePresentation{}, nullptr);
}

void FieldValueRequestCoordinator::OnChallengePresentation(
    const std::string& request_id,
    ResolvedNodeFacts facts,
    std::optional<FieldChallengePresentation> presentation,
    const char* capture_refused_at) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  it->second.awaiting_challenge_presentation = false;
  it->second.cancel_challenge_presentation.Reset();
  const std::string task_id = it->second.task_id;
  if (const char* gap = PresentationGap(facts, presentation)) {
    if (facts.node_id.value != it->second.named_node_id &&
        !it->second.fields.empty() &&
        it->second.fields.back().node_id == facts.node_id.value) {
      // A companion the sheet cannot show — most often a CAPTCHA whose
      // picture is below the fold. Its row is left off and the rest are
      // still asked for; the snapshot goes on listing it as a field only the
      // person can supply, so the task asks again once it is in view. The
      // named field keeps the old rule below, because its outcome is the
      // task's instruction for what to do next (decision 0215).
      LOG(WARNING) << "[taffy_field_request_companion_dropped] at=" << gap;
      it->second.fields.pop_back();
      ResolveNextCandidate(request_id);
      return;
    }
    AbandonRequest(request_id, task_id, CloseReason::kRevoked, gap,
                   ChallengeOutcome(capture_refused_at));
    return;
  }

  auto descriptor = surface_mojom::FieldValueDescriptor::New();
  descriptor->field_id = facts.node_id.value;
  descriptor->label = facts.display_label;
  descriptor->masked = true;
  descriptor->challenge = SurfaceChallenge(facts.challenge_kind);
  if (presentation) {
    if (!presentation->image_png.empty()) {
      descriptor->challenge_image = std::move(presentation->image_png);
    }
    if (presentation->highlight) {
      descriptor->highlight = surface_mojom::FieldHighlight::New(
          presentation->highlight->left, presentation->highlight->top,
          presentation->highlight->right, presentation->highlight->bottom);
    }
  }

  it->second.descriptors.push_back(std::move(descriptor));
  ResolveNextCandidate(request_id);
}

void FieldValueRequestCoordinator::OpenPreparedRequest(
    const std::string& request_id) {
  const auto it = open_.find(request_id);
  if (it == open_.end()) {
    return;
  }
  const std::string task_id = it->second.task_id;
  const std::optional<FieldValueDocument> document =
      resolve_document_.Run(it->second.tab_id);
  const char* at = nullptr;
  AskOutcome outcome = AskOutcome::kCannotBeShown;
  if (!document || !SamePage(it->second.document, *document)) {
    at = "page-moved";
    outcome = AskOutcome::kPageMoved;
  } else if (it->second.fields.empty()) {
    // Every candidate fell out, so there was nothing on this page the sheet
    // could have been about.
    at = "nothing-to-ask";
    outcome = AskOutcome::kNotAField;
  } else if (it->second.fields.size() != it->second.descriptors.size()) {
    at = "field-lost";
  }
  if (at) {
    AbandonRequest(request_id, task_id, CloseReason::kRevoked, at, outcome);
    return;
  }
  it->second.document = *document;
  auto request = surface_mojom::FieldValueRequest::New();
  request->request_id = request_id;
  request->task_id = task_id;
  request->host = document->host;
  request->fields = std::move(it->second.descriptors);
  request->approval_lifetime_seconds = kFieldValueApprovalLifetimeSeconds;
  LOG(WARNING) << "[taffy_field_request_opened] fields="
               << request->fields.size()
               << " companions=" << it->second.companion_node_ids.size();
  client_->Open(std::move(request));
}

}  // namespace taffy
