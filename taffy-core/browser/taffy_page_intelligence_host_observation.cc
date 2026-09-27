// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_broker.h"

namespace taffy {
namespace {

struct ExactObservationProjection {
  std::vector<AdapterRequirement> adapters;
  std::vector<AdapterKind> allowed_adapters;
  std::vector<SemanticField> fields;
  const char* sensitivity_policy_id = nullptr;
  const char* task_purpose = nullptr;
};

std::optional<ExactObservationProjection> ProjectionForTarget(
    const AuthorizedObservationTarget& target) {
  if (!target.is_valid()) {
    return std::nullopt;
  }
  if (target.kind == AuthorizedObservationKind::kPageScreenshot) {
    return ExactObservationProjection{
        .adapters =
            {
                AdapterRequirement{AdapterKind::kDom,
                                   AdapterRequirementLevel::kOptional},
                AdapterRequirement{AdapterKind::kAccessibility,
                                   AdapterRequirementLevel::kOptional},
                AdapterRequirement{AdapterKind::kForms,
                                   AdapterRequirementLevel::kOptional},
                AdapterRequirement{AdapterKind::kMetadata,
                                   AdapterRequirementLevel::kOptional},
                AdapterRequirement{AdapterKind::kLayout,
                                   AdapterRequirementLevel::kOptional},
            },
        .allowed_adapters = {AdapterKind::kDom, AdapterKind::kAccessibility,
                             AdapterKind::kForms, AdapterKind::kMetadata,
                             AdapterKind::kLayout},
        .fields = {SemanticField::kRole, SemanticField::kName,
                   SemanticField::kTextRuns, SemanticField::kStates,
                   SemanticField::kValueDescriptor, SemanticField::kBounds,
                   SemanticField::kSensitivity, SemanticField::kChallengeKind},
        .sensitivity_policy_id = "taffy_page_screenshot_fallback",
        .task_purpose = "the task requested a bounded page screenshot fallback",
    };
  }
  switch (target.scope) {
    case ObservationScope::kDocument:
      return ExactObservationProjection{
          .adapters =
              {
                  AdapterRequirement{AdapterKind::kDom,
                                     AdapterRequirementLevel::kRequired},
                  AdapterRequirement{AdapterKind::kAccessibility,
                                     AdapterRequirementLevel::kOptional},
                  AdapterRequirement{AdapterKind::kForms,
                                     AdapterRequirementLevel::kOptional},
                  AdapterRequirement{AdapterKind::kMetadata,
                                     AdapterRequirementLevel::kOptional},
              },
          .allowed_adapters = {AdapterKind::kDom, AdapterKind::kAccessibility,
                               AdapterKind::kForms, AdapterKind::kMetadata},
          .fields = {SemanticField::kRole, SemanticField::kName,
                     SemanticField::kTextRuns, SemanticField::kStates,
                     SemanticField::kActions, SemanticField::kDestination,
                     SemanticField::kSensitivity, SemanticField::kFrames,
                     SemanticField::kContentTrust,
                     SemanticField::kContentSignals},
          .sensitivity_policy_id = "taffy_read_page",
          .task_purpose = "the task requested a page observation",
      };
    case ObservationScope::kSection:
      return ExactObservationProjection{
          .adapters =
              {
                  AdapterRequirement{AdapterKind::kDom,
                                     AdapterRequirementLevel::kOptional},
                  AdapterRequirement{AdapterKind::kForms,
                                     AdapterRequirementLevel::kRequired},
                  AdapterRequirement{AdapterKind::kLayout,
                                     AdapterRequirementLevel::kOptional},
              },
          .allowed_adapters = {AdapterKind::kDom, AdapterKind::kForms,
                               AdapterKind::kLayout},
          .fields = {SemanticField::kRole, SemanticField::kName,
                     SemanticField::kDescription, SemanticField::kTextRuns,
                     SemanticField::kStates, SemanticField::kValueDescriptor,
                     SemanticField::kDestination, SemanticField::kBounds,
                     SemanticField::kActions, SemanticField::kAttributes,
                     SemanticField::kSensitivity, SemanticField::kEdges},
          .sensitivity_policy_id = "taffy_form_inspect",
          .task_purpose = "the task requested one exact form subtree",
      };
    case ObservationScope::kSelection:
      return ExactObservationProjection{
          .adapters =
              {
                  AdapterRequirement{AdapterKind::kAccessibility,
                                     AdapterRequirementLevel::kRequired},
                  AdapterRequirement{AdapterKind::kSelection,
                                     AdapterRequirementLevel::kRequired},
              },
          .allowed_adapters = {AdapterKind::kAccessibility,
                               AdapterKind::kSelection},
          .fields = {SemanticField::kRole, SemanticField::kName,
                     SemanticField::kDescription, SemanticField::kTextRuns,
                     SemanticField::kStates, SemanticField::kSensitivity,
                     SemanticField::kEdges},
          .sensitivity_policy_id = "taffy_selection_read",
          .task_purpose = "the task requested the live user selection",
      };
    case ObservationScope::kViewport:
    case ObservationScope::kInteractive:
      return std::nullopt;
  }
  return std::nullopt;
}

// Every field is non-zero on purpose. Zero means "unset" and is filled from
// the process ceiling, so a budget that left one zero would silently be asking
// for the ceiling.
//
// `max_nodes` is the Core Service observation ceiling, not a tighter local
// number. A grant that asked for 500 while the contract asked for 1500 would
// cut every page to 500 nodes and the 1500 would be decorative: that is how
// a search results page lost every result (decision 0144).
ObservationBudget OnDemandBudget() {
  ObservationBudget budget;
  budget.max_nodes =
      static_cast<uint32_t>(core_service::mojom::kMaxTaskObservationNodes);
  budget.max_text_bytes = 64 * 1024;
  budget.max_total_bytes = 256 * 1024;
  // The traversal ceiling the renderer endpoint declares in
  // `//taffy/renderer/observation_limits.json`, and not permitted to be
  // stricter than it: depth is a shape bound, so cutting it below what the
  // endpoint serves removes the deepest subtree and saves no work at all
  // (decision 0170). This is the third place that number is written and the
  // one that actually binds — the task effect narrows nodes, bytes, frames and
  // the deadline and carries no depth, so whatever stands here is the depth
  // every task observation gets. It stood at 32 while the endpoint said 64,
  // which is why a search results page still arrived truncated after the
  // browser's own clamp was corrected.
  budget.max_depth = 64;
  budget.max_frames = 8;
  budget.deadline_ms = 1500;
  return budget;
}

}  // namespace

RequestId TaffyPageIntelligenceHost::RequestObservation(
    const core_service::mojom::PageObservationEffect& effect,
    ActorLeaseRegistry& actor_leases,
    CapabilityLedger& capabilities,
    std::optional<AuthorizedObservationTarget>* authorized_target,
    ObservationCompletion callback) {
  if (authorized_target) {
    authorized_target->reset();
  }
  auto refuse = [&callback]() {
    std::move(callback).Run(std::nullopt);
    return RequestId{};
  };
  if (!authorized_target) {
    return refuse();
  }
  // A refusal that arrived as a grant is still a refusal. Nothing below is
  // reachable for an origin policy-engine declined, and the check is here as
  // well as at the caller because this is the last place before a renderer is
  // asked for anything.
  content::WebContents* web_contents = observed_web_contents();
  content::RenderFrameHost* frame = web_contents->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    return refuse();
  }
  PageIntelligenceBroker* broker =
      PageIntelligenceBroker::FromWebContents(web_contents);
  if (!broker || !service_) {
    return refuse();
  }

  const FrameId root_frame_id = broker->GetOrAssignFrameId(frame);
  if (!root_frame_id.is_valid() || root_frame_id.value != effect.frame_id) {
    return refuse();
  }
  FrameObservationEndpoint* endpoint =
      broker->GetOrCreateActionableEndpoint(root_frame_id);
  if (!endpoint || endpoint->page_epoch().value != effect.page_epoch) {
    return refuse();
  }

  // The freshness requirement, and what "no requirement" is allowed to mean.
  //
  // A task naming a floor must name one this endpoint has reached: the floor
  // came from an earlier observation of this document, and a floor ahead of
  // what the renderer has reported describes a document this browser has not
  // seen.
  //
  // A task naming no floor is admitted only when there is no floor to name —
  // when the renderer has reported nothing for this document, which is exactly
  // the first observation. Past that point an unpinned read would be a task
  // asking to see a document without saying which version of it, on a document
  // known to have moved at least once.
  //
  // Direct user intent is the other way round and always has been: it carries
  // no floor by construction, because a person asking to look at what is in
  // front of them is not refreshing a binding.
  using AuthoritySubjectKind = core_service::mojom::AuthoritySubjectKind;
  const bool task_subject =
      effect.authority_subject &&
      effect.authority_subject->kind == AuthoritySubjectKind::kTask;
  const bool direct_subject =
      effect.authority_subject &&
      effect.authority_subject->kind == AuthoritySubjectKind::kDirectUserIntent;
  if (!effect.authority_subject ||
      (task_subject && effect.expected_graph_revision == 0u &&
       endpoint->last_reported_revision() != 0u) ||
      (task_subject && effect.expected_graph_revision != 0u &&
       endpoint->last_reported_revision() < effect.expected_graph_revision) ||
      (direct_subject && effect.expected_graph_revision != 0u)) {
    return refuse();
  }

  const Origin origin =
      OriginCodec::Get().ToWireOrigin(frame->GetLastCommittedOrigin());
  if (!origin.is_valid() || origin.is_opaque() ||
      broker->tab_id().value != effect.tab_id) {
    return refuse();
  }

  // Spend the already registered Rust-minted capability before a renderer
  // request exists. The committed origin joins the tab/frame/page tuple here,
  // at the last browser-owned point where navigation truth is available.
  AuthorizedObservationTarget target;
  if (capabilities.AdmitObservation(effect, origin.serialization, actor_leases,
                                    base::TimeTicks::Now(), &target) !=
          CapabilityAdmission::kAdmitted ||
      !target.is_valid()) {
    return refuse();
  }
  std::optional<ExactObservationProjection> projection =
      ProjectionForTarget(target);
  if (!projection) {
    return refuse();
  }

  ObservationPolicyGrant grant;
  grant.budget = OnDemandBudget();
  if (effect.max_bytes != 0) {
    grant.budget.max_total_bytes =
        std::min(grant.budget.max_total_bytes, effect.max_bytes);
  }
  if (effect.max_nodes != 0u) {
    grant.budget.max_nodes = std::min(grant.budget.max_nodes, effect.max_nodes);
  }
  if (effect.max_text_bytes != 0u) {
    grant.budget.max_text_bytes =
        std::min(grant.budget.max_text_bytes, effect.max_text_bytes);
  }
  if (effect.max_frames != 0u) {
    grant.budget.max_frames =
        std::min(grant.budget.max_frames, effect.max_frames);
  }
  if (effect.deadline_ms != 0u) {
    grant.budget.deadline_ms =
        std::min(grant.budget.deadline_ms, effect.deadline_ms);
  }
  grant.max_scope = target.scope;
  // The document this grant is about. Exactly the origin the browser
  // committed, and no other, so a document that navigated between the decision
  // and the request is refused by the grant rather than by luck.
  //
  // Until 2026-09-07 this line set `allowed_origins` alone and claimed that
  // sentence for it. It was not true: that list is the cross-origin
  // child-frame allowlist and the root is never measured against it, so the
  // refusal the comment promised did not exist anywhere. What covered this
  // path in practice was `expected_page_epoch` a few lines down plus the fact
  // that both are minted in one synchronous block — a real defence, but not
  // the one being described, and not one a second caller of SubmitObservation
  // would inherit.
  grant.document_origin = origin;
  grant.allowed_origins = {origin};
  grant.may_include_child_frames = false;
  // Nothing above the not-sensitive class, ever, on this path. Widening it is
  // a policy decision with its own approval path and no browser-process code
  // is entitled to make it.
  grant.max_sensitivity = Sensitivity::kNotSensitive;
  grant.allowed_adapters = projection->allowed_adapters;
  ObservationRequest request;
  switch (effect.authority_subject->kind) {
    case core_service::mojom::AuthoritySubjectKind::kTask:
      if (effect.task_id.empty() || effect.action_id.empty() ||
          effect.authority_subject->authority_subject_id != effect.task_id) {
        return refuse();
      }
      request.authority_subject =
          ObservationAuthoritySubject::ForTask(TaskId{effect.task_id});
      break;
    case core_service::mojom::AuthoritySubjectKind::kDirectUserIntent:
      if (!effect.task_id.empty() || !effect.action_id.empty()) {
        return refuse();
      }
      request.authority_subject =
          ObservationAuthoritySubject::ForDirectUserIntent(
              DirectIntentId{effect.authority_subject->authority_subject_id});
      break;
  }
  if (!request.authority_subject.is_valid()) {
    return refuse();
  }
  request.tab_id = broker->tab_id();
  request.root_frame_id = root_frame_id;
  request.expected_page_epoch = endpoint->page_epoch();
  request.scope = target.scope;
  request.form_root = target.form_root;
  request.media_root = target.media_root;
  request.adapters = std::move(projection->adapters);
  request.requested_fields = std::move(projection->fields);
  request.include_child_frames = false;
  request.allowed_origins = {origin};
  request.budget = grant.budget;
  request.sensitivity_policy_id =
      SensitivityPolicyId{projection->sensitivity_policy_id};
  // Carried into the journal and the audit record, never onto the wire. It
  // says why a person's browser read a page, in the words a person would use.
  request.task_purpose =
      request.authority_subject.kind ==
              ObservationAuthoritySubjectKind::kDirectUserIntent
          ? "the person asked what is on this page"
          : projection->task_purpose;

  if (registering_observation_ || synchronous_observation_result_) {
    return refuse();
  }
  *authorized_target = target;
  registering_observation_ = true;
  const RequestId request_id =
      service_->SubmitObservation(std::move(request), std::move(grant));
  registering_observation_ = false;
  if (!request_id.is_valid()) {
    return refuse();
  }
  if (synchronous_observation_result_) {
    ObservationEnvelope result = std::move(*synchronous_observation_result_);
    synchronous_observation_result_.reset();
    if (result.request_id != request_id) {
      ++observations_refused_;
      return refuse();
    }
    DeliverObservation(std::move(result), std::move(callback));
    return request_id;
  }
  if (pending_observations_.contains(request_id)) {
    ++observations_refused_;
    return refuse();
  }
  pending_observations_.emplace(request_id, std::move(callback));
  return request_id;
}

bool TaffyPageIntelligenceHost::CancelObservation(RequestId request_id) {
  if (!request_id.is_valid() || !service_ ||
      !pending_observations_.contains(request_id)) {
    return false;
  }
  service_->Cancel(std::move(request_id));
  return true;
}

void TaffyPageIntelligenceHost::OnObservationResult(
    ObservationEnvelope result) {
  ++observations_seen_;

  if (registering_observation_) {
    if (synchronous_observation_result_) {
      ++observations_refused_;
    } else {
      synchronous_observation_result_ = std::move(result);
    }
    return;
  }

  auto entry = pending_observations_.find(result.request_id);
  if (entry == pending_observations_.end()) {
    ++observations_refused_;
    return;
  }
  ObservationCompletion callback = std::move(entry->second);
  pending_observations_.erase(entry);

  DeliverObservation(std::move(result), std::move(callback));
}

void TaffyPageIntelligenceHost::DeliverObservation(
    ObservationEnvelope result,
    ObservationCompletion callback) {
  // Replace, never merge: a link authority belongs to one exact complete
  // snapshot. Failed or partial observations deliberately clear an older
  // registry so a later action cannot inherit a destination it did not see.
  observed_links_.Replace(result);

  // Content-free bring-up evidence, and the one line that tells a person
  // debugging this which half failed. Every value is a compiled-in enumeration
  // member or a count: the result code, how many nodes the browser was willing
  // to describe, whether a graph travelled at all, and - when the answer was
  // partial - which budget ran out and how much it left behind. None of them
  // is a fact about what was on the page.
  //
  // The truncation half is here because it was missing, and its absence cost a
  // day. A task read one search results page eighty-five times and never left
  // it; the line said "result code 2, nodes 128" every time, and code 2 is
  // kIncomplete, which is the envelope reporting that a budget cut the walk
  // short. Which budget is what says whether the fix is a deadline, a node
  // ceiling or a byte ceiling, and that is exactly the field this line did not
  // print. A truncation is never reported without a named cause
  // (observation_result_builder.cc), so budgets_reached is non-empty whenever
  // truncated is set and this can never be an empty list beside a true flag.
  std::string budgets;
  for (BudgetKind budget : result.truncation.budgets_reached) {
    if (!budgets.empty()) {
      budgets += "+";
    }
    budgets += base::NumberToString(static_cast<int>(budget));
  }
  // The same argument as the truncation half, one layer up. A required
  // adapter that is absent makes the whole observation UNSUPPORTED
  // (adapter_requirement_check.cc), and that verdict outranks every count
  // printed below it: a graph of two hundred nodes travelled and the task
  // still ended "Taffy could not read enough to answer". Which adapter, and
  // in what state, is the fact that says whether the fix is a capability, a
  // budget or a document. Both values are compiled-in enumeration members.
  std::string adapters;
  for (const AdapterReport& report : result.adapters) {
    if (!adapters.empty()) {
      adapters += "+";
    }
    adapters += base::NumberToString(static_cast<int>(report.adapter));
    adapters += ":";
    adapters += base::NumberToString(static_cast<int>(report.status));
  }
  std::string warnings;
  for (uint8_t warning : result.warning_codes) {
    if (!warnings.empty()) {
      warnings += "+";
    }
    warnings += base::NumberToString(static_cast<int>(warning));
  }
  LOG(INFO) << "Taffy: observation result code "
            << static_cast<int>(result.code) << ", nodes " << result.node_count
            << ", frames " << result.frames.size() << ", encoding "
            << static_cast<int>(result.encoding) << ", graph bytes "
            << result.graph_payload.size() << ", truncated "
            << result.truncation.truncated << ", budgets ["
            << (budgets.empty() ? "none" : budgets) << "], omitted nodes "
            << result.truncation.omitted_node_count << ", omitted text bytes "
            << result.truncation.omitted_text_bytes << ", may change answer "
            << result.truncation.may_change_answer << ", adapters ["
            << (adapters.empty() ? "none" : adapters) << "], warnings ["
            << (warnings.empty() ? "none" : warnings) << "]";

  ++observations_submitted_;
  std::move(callback).Run(
      std::optional<ObservationEnvelope>(std::move(result)));
}

}  // namespace taffy
