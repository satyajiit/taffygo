// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The observation half of PageIntelligenceServiceImpl.
//
// A second translation unit for one class, which is unusual enough to justify.
// The class has two responsibilities that are read by different people at
// different times: the request-and-reply plumbing for one observation, which is
// here, and the service surface — negotiation, actions, leases, grants,
// subscriptions, memory pressure and broker notifications — which is in
// page_intelligence_service_impl.cc. Keeping both in one file put it past the
// size at which //taffy treats a file as a design smell, and the
// seam between them is the one place the file naturally divides: nothing here
// is called from there except through the header.
//
// Negotiation moved across that seam on 2026-09-07. It had been described here
// as part of the same plumbing, and it is not: QueryProtocolSupport is a
// service entry point like Subscribe, it settles through the sink rather than
// through this file's pending table, and keeping it here was what put this
// file back over the line cap the split exists to respect.

#include <algorithm>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/cpp/is_potentially_trustworthy.h"
#include "taffy/components/intelligence/content/adapter_requirement_check.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/event_sequence_tracker.h"
#include "taffy/components/intelligence/content/frame_observation_endpoint.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/origin_codec.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

// The URL projection the browser is willing to hand onward. Full URLs, queries
// and fragments appear only when task and provider policy permit them
// (protocol section 7.2); until the policy plumbing lands, the browser
// discloses the origin and nothing else, which is the conservative direction.
//
// VERIFY AT SP-04: the disclosure decision belongs to the sensitivity policy
// identified by ObservationRequest::sensitivity_policy_id. Wiring that policy
// through is M2 work; hard-coding origin-only here is deliberate under-
// disclosure, never over-disclosure.
UrlMetadata BuildUrlMetadata(const GURL& committed_url,
                             const url::Origin& origin) {
  UrlMetadata metadata;
  metadata.origin = OriginCodec::Get().ToWireOrigin(origin);
  metadata.disclosure = UrlDisclosure::kOriginOnly;
  metadata.has_query = committed_url.has_query();
  metadata.has_fragment = committed_url.has_ref();
  return metadata;
}

bool FormObservationShapeIsValid(const ObservationRequest& request) {
  const bool section = request.scope == ObservationScope::kSection;
  if (section != request.form_root.has_value()) {
    return false;
  }
  return !request.form_root.has_value() ||
         (request.form_root->node_id.is_valid() &&
          GraphRevisionFloorIsWellFormed(
              /*targets_node=*/true,
              request.form_root->minimum_graph_revision));
}

bool MediaObservationShapeIsValid(const ObservationRequest& request) {
  if (!request.media_root.has_value()) {
    return true;
  }
  return request.scope == ObservationScope::kDocument &&
         !request.form_root.has_value() &&
         request.media_root->node_id.is_valid() &&
         GraphRevisionFloorIsWellFormed(
             /*targets_node=*/true, request.media_root->minimum_graph_revision);
}

// Closed budget and request-shape facts only; no page or authority identities.
void LogMemoryRefusal(const MemoryPressureGovernor& memory,
                      const ObservationRequest& request) {
  LOG(INFO) << "[taffy_bip_memory_refusal] limit=" << memory.memory_limit()
            << " stage=" << static_cast<int>(memory.stage())
            << " scope=" << static_cast<int>(request.scope)
            << " form_root=" << request.form_root.has_value()
            << " media_root=" << request.media_root.has_value();
}

}  // namespace

// --- observation ------------------------------------------------------------

RequestId PageIntelligenceServiceImpl::SubmitObservation(
    ObservationRequest request,
    ObservationPolicyGrant grant) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const RequestId request_id = MintRequestId("req_");

  if (!request.authority_subject.is_valid()) {
    RefuseObservation(request_id, ObservationResultCode::kInternalError,
                      "authority_subject");
    return request_id;
  }
  if (!FormObservationShapeIsValid(request) ||
      !MediaObservationShapeIsValid(request)) {
    RefuseObservation(request_id, ObservationResultCode::kUnsupported,
                      "observation_shape");
    return request_id;
  }

  // Memory pressure refuses observations only after delta streams have already
  // stopped, which is the order protocol section 15 fixes. The code is
  // recoverable and says so.
  if (memory_.AdmitObservation()) {
    LogMemoryRefusal(memory_, request);
    RefuseObservationForMemoryPressure(request_id, request);
    return request_id;
  }

  // Clamp before anything leaves the browser process (protocol section 7.1),
  // then narrow again for whatever the device is short of.
  ObservationRequest clamped =
      ClampObservationRequest(request, grant, GetProcessBudgetLimits());
  clamped.request_id = request_id;
  // An exact form read cannot be substituted with a viewport read if policy
  // granted less than requested. The latter may be narrower by breadth, but it
  // is a different set of nodes and therefore not the request the capability
  // authorized.
  if ((request.form_root.has_value() || request.media_root.has_value()) &&
      clamped.scope != request.scope) {
    RefuseObservation(request_id, ObservationResultCode::kUnsupported,
                      "exact_read_narrowed");
    return request_id;
  }
  const ObservationScope pressure_scope = memory_.NarrowScope(clamped.scope);
  if ((clamped.form_root.has_value() || clamped.media_root.has_value()) &&
      pressure_scope != clamped.scope) {
    LogMemoryRefusal(memory_, clamped);
    RefuseObservationForMemoryPressure(request_id, clamped);
    return request_id;
  }
  clamped.scope = pressure_scope;
  if (!memory_.AllowsOptionalAdapters()) {
    std::vector<AdapterRequirement> required;
    for (const AdapterRequirement& adapter : clamped.adapters) {
      if (adapter.requirement == AdapterRequirementLevel::kRequired) {
        required.push_back(adapter);
      }
    }
    clamped.adapters = std::move(required);
  }

  // An observation is a document's first use, so this allocates the endpoint
  // when the document does not have one yet — see
  // PageIntelligenceBroker::GetOrCreateActionableEndpoint for why the
  // allocating accessor is the correct one here and why it still cannot revive
  // a retired document.
  FrameObservationEndpoint* endpoint =
      broker_->GetOrCreateActionableEndpoint(clamped.root_frame_id);
  if (!endpoint || !endpoint->remote().is_bound()) {
    RefuseObservation(request_id, ObservationResultCode::kDocumentInactive,
                      "no_endpoint");
    return request_id;
  }

  // The grant names one document, and this is where the browser checks that it
  // is still the document in front of it. A redirect is the case this exists
  // for: the scope was decided against the site the person asked for, the
  // server answered with a different site, and without this the observation of
  // the second one was admitted by the first one's grant.
  //
  // After the liveness check above rather than before it, because a document
  // that is gone is not a document on the wrong origin, and answering the
  // second when the first is true would send a caller to re-ask policy for a
  // frame that has no renderer.
  //
  // Nothing here re-decides anything. The grant is spent or refused, never
  // widened, which is the browser half of decision 0033.
  //
  // The code is `kStalePageEpoch`, not `kUnsupported`. Both are refusals, but
  // they instruct the caller to do opposite things: `kUnsupported` means this
  // build does not serve the request and its recovery is do-not-retry, while
  // this is a document that moved on between the grant being minted and the
  // request being spent — which is what a fresh look is for. A phone showed
  // the cost. A task navigated, asked policy 30 ms later, and submitted the
  // read 35 ms after that; the site had redirected within its own registrable
  // domain in that window, this branch answered `kUnsupported`, and the one
  // refused bootstrap read ended the errand as "Taffy could not read enough to
  // answer". Decision 0164.
  if (!GrantNamesTheCommittedDocument(grant, clamped.root_frame_id)) {
    RefuseObservation(request_id, ObservationResultCode::kStalePageEpoch,
                      "grant_document_moved");
    return request_id;
  }
  // An observation that may describe child frames needs identity for them
  // before the tree is assembled, for the same "first use" reason the root
  // endpoint above is allocated here. Without it the frame list silently omits
  // every child no one had observed on its own.
  if (clamped.include_child_frames) {
    broker_->EnsureFrameTreeIdentity(clamped.root_frame_id);
  }

  // A refresh names the epoch it expects. A mismatch is stale, not a silent
  // rebind onto whatever document is there now.
  if (clamped.expected_page_epoch.has_value() &&
      *clamped.expected_page_epoch != endpoint->page_epoch()) {
    RefuseObservation(request_id, ObservationResultCode::kStalePageEpoch,
                      "expected_page_epoch");
    return request_id;
  }

  // A required adapter the endpoint does not implement makes the request
  // unsupported now, rather than after an extraction that could not have
  // produced a usable answer (protocol section 6.2).
  if (const ProtocolSupportEnvelope* support =
          negotiator_->GetSupport(clamped.root_frame_id)) {
    const AdapterRequirementOutcome outcome = CheckAdaptersAgainstSupport(
        clamped.adapters, *support, grant.allowed_adapters);
    if (outcome.code == ObservationResultCode::kUnsupported) {
      RefuseObservation(request_id, ObservationResultCode::kUnsupported,
                        "required_adapter");
      return request_id;
    }
  }

  auto wire = mojom::SnapshotRequest::New();
  wire->schema_version = kBipSchemaVersion;
  wire->request_id = request_id.value;
  wire->profile_id = std::string();
  wire->tab_id = clamped.tab_id.value;
  wire->root_frame_id = clamped.root_frame_id.value;
  // Which frame this request is addressed to, read from the endpoint it is
  // being sent to rather than from the request. Today the two are the same
  // value, because the broker addresses the endpoint it named as the root; the
  // day a request fans out to child frames they stop being the same, and this
  // line needs no change for that, because whatever endpoint receives it is
  // the one that named itself here.
  //
  // Without it a renderer has no way to tell whether it is the root: its own
  // frame identity is in a namespace this process does not share, so comparing
  // it against root_frame_id answered "different" for every document ever
  // observed and pushed every node to the sensitivity floor.
  wire->observed_frame_id = endpoint->frame_id().value;
  // Always the broker's epoch, never the caller's.
  //
  // The two are not the same thing even though they share a field name. The
  // caller's `expected_page_epoch` is a precondition and was checked against
  // this endpoint a few lines above, where a mismatch is kStalePageEpoch. What
  // travels on the wire is the browser's own answer to "which document is
  // this", because the epoch is the renderer's node-id namespace and the
  // renderer refuses to allocate into a namespace it cannot name
  // (renderer/page_intelligence_endpoint.cc, and the coordination item at
  // renderer/README.md item 4).
  //
  // Forwarding the caller's optional instead meant the first observation of
  // any document - the one case where a caller cannot yet know the epoch,
  // because the epoch is what an observation returns - arrived with the field
  // unset and was refused kStalePageEpoch. Sending what the broker allocated
  // is not inventing a fact; it is the browser stating the one identity it
  // owns.
  wire->expected_page_epoch = endpoint->page_epoch().value;
  wire->scope = ToMojom(clamped.scope);
  for (const AdapterRequirement& adapter : clamped.adapters) {
    auto requirement = mojom::AdapterRequirement::New();
    requirement->adapter = ToMojom(adapter.adapter);
    requirement->requirement =
        static_cast<mojom::AdapterRequirementLevel>(adapter.requirement);
    wire->adapters.push_back(std::move(requirement));
  }
  for (SemanticField field : clamped.requested_fields) {
    wire->requested_fields.push_back(static_cast<mojom::SemanticField>(field));
  }
  wire->include_child_frames = clamped.include_child_frames;
  for (const Origin& origin : clamped.allowed_origins) {
    wire->allowed_origins.push_back(ToMojom(origin));
  }
  wire->max_nodes = clamped.budget.max_nodes;
  wire->max_text_bytes = clamped.budget.max_text_bytes;
  wire->max_total_bytes = clamped.budget.max_total_bytes;
  wire->max_depth = clamped.budget.max_depth;
  wire->deadline_ms = clamped.budget.deadline_ms;
  wire->sensitivity_policy_id = clamped.sensitivity_policy_id.value;
  wire->task_purpose = clamped.task_purpose;
  if (clamped.form_root.has_value()) {
    wire->form_root = mojom::FormObservationRoot::New();
    wire->form_root->node_id = clamped.form_root->node_id.value;
    wire->form_root->minimum_graph_revision =
        clamped.form_root->minimum_graph_revision;
  }
  if (clamped.media_root.has_value()) {
    wire->media_root = mojom::MediaObservationRoot::New();
    wire->media_root->node_id = clamped.media_root->node_id.value;
    wire->media_root->minimum_graph_revision =
        clamped.media_root->minimum_graph_revision;
  }

  PendingObservation pending;
  pending.request_id = request_id;
  pending.authority_subject = request.authority_subject;
  pending.grant = std::move(grant);
  pending.expected_epoch = endpoint->page_epoch();
  pending.started_at = base::TimeTicks::Now();
  pending.deadline = RendererCallDeadline::Arm(
      RendererDeadlineFor(clamped.budget),
      base::BindOnce(&PageIntelligenceServiceImpl::FinishObservationWithCode,
                     weak_factory_.GetWeakPtr(), request_id,
                     ObservationResultCode::kDeadlineExceeded));
  // The callback only needs this one identity after the pending record takes
  // ownership. Moving the request avoids copying every adapter, origin and
  // requested field into the in-flight table on the browser UI thread.
  const FrameId addressed_frame_id = clamped.root_frame_id;
  pending.clamped = std::move(clamped);
  scoped_refptr<RendererCallDeadline> guard = pending.deadline;
  pending_observations_.emplace(request_id, std::move(pending));

  endpoint->remote()->GetSnapshot(
      std::move(wire),
      BindReplyWithDeadline(
          std::move(guard),
          base::BindOnce(&PageIntelligenceServiceImpl::OnSnapshotResult,
                         weak_factory_.GetWeakPtr(), request_id,
                         addressed_frame_id)));
  return request_id;
}

BrowserOwnedObservationFacts
PageIntelligenceServiceImpl::CollectBrowserOwnedFacts(
    const ObservationRequest& clamped) const {
  BrowserOwnedObservationFacts facts;
  content::RenderFrameHost* host = broker_->ResolveFrame(clamped.root_frame_id);
  if (!host) {
    return facts;
  }
  const url::Origin origin = host->GetLastCommittedOrigin();
  facts.origin = OriginCodec::Get().ToWireOrigin(origin);
  facts.committed_url_metadata =
      BuildUrlMetadata(host->GetLastCommittedURL(), origin);
  facts.is_potentially_trustworthy =
      network::IsOriginPotentiallyTrustworthy(origin);
  facts.is_incognito =
      host->GetBrowserContext() && host->GetBrowserContext()->IsOffTheRecord();
  facts.frames = broker_->BuildFrameTree(clamped.root_frame_id);
  if (FrameObservationEndpoint* endpoint =
          FrameObservationEndpoint::GetForCurrentDocument(host)) {
    facts.page_epoch = endpoint->page_epoch();
  }
  return facts;
}

FrameInclusionInputs PageIntelligenceServiceImpl::BuildFrameInclusionInputs(
    const ObservationRequest& clamped,
    const ObservationPolicyGrant& grant,
    const Origin& root_origin) const {
  FrameInclusionInputs inputs;
  inputs.root_origin = root_origin;
  inputs.scope = clamped.scope;
  inputs.include_child_frames = clamped.include_child_frames;
  inputs.allowed_origins = clamped.allowed_origins;
  inputs.max_frames = clamped.budget.max_frames;
  return inputs;
}

void PageIntelligenceServiceImpl::OnSnapshotResult(
    RequestId request_id,
    FrameId frame_id,
    mojom::SnapshotResultPtr result) {
  auto it = pending_observations_.find(request_id);
  if (it == pending_observations_.end()) {
    // Cancelled, timed out, or invalidated by navigation. The reply is dropped
    // and counted: protocol section 6.3 requires both halves, and a drop that
    // nobody counted is indistinguishable from a renderer that never answered.
    if (FrameObservationEndpoint* late =
            broker_->GetActionableEndpoint(frame_id)) {
      late->RecordLateReply();
    }
    return;
  }
  // Borrow the pending request until FinishObservation removes it. Copying
  // these two records duplicated all adapter/origin/field vectors on every
  // successful renderer reply, immediately before the pending copy was
  // destroyed.
  const ObservationRequest& clamped = it->second.clamped;
  const ObservationPolicyGrant& grant = it->second.grant;

  ObservationEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.request_id = request_id;
  envelope.tab_id = clamped.tab_id;
  envelope.root_frame_id = clamped.root_frame_id;
  envelope.scope = clamped.scope;
  envelope.capture_time_monotonic_ms = NowMonotonicMs();

  if (!result || !result->snapshot) {
    envelope.code = result ? static_cast<ObservationResultCode>(result->code)
                           : ObservationResultCode::kInternalError;
    // The renderer puts the warnings that have no snapshot to travel on right
    // here, and this branch dropped every one of them. A phone paid for that:
    // three reads of a page that had not finished parsing came back "result
    // code 1, adapters [none], warnings [none]", which reads as a build that
    // cannot describe the page, and the renderer had said in as many words
    // that one required adapter was unavailable. Only the closed code member
    // is carried; `detail_code` is renderer-authored text and stays where the
    // snapshot path scans it.
    if (result) {
      for (const mojom::SnapshotWarningPtr& warning : result->warnings) {
        if (warning) {
          envelope.warning_codes.push_back(static_cast<uint8_t>(warning->code));
        }
      }
    }
    FinishObservation(request_id, std::move(envelope));
    return;
  }

  const mojom::PageSnapshot& snapshot = *result->snapshot;

  // The epoch the broker holds now, not the one the request was issued
  // against: a document that changed under the request must not be described
  // as if it had not.
  FrameObservationEndpoint* endpoint =
      broker_->GetActionableEndpoint(clamped.root_frame_id);
  if (!endpoint) {
    envelope.code = ObservationResultCode::kDocumentInactive;
    FinishObservation(request_id, std::move(envelope));
    return;
  }
  if (endpoint->page_epoch() != it->second.expected_epoch) {
    envelope.code = ObservationResultCode::kStalePageEpoch;
    FinishObservation(request_id, std::move(envelope));
    return;
  }

  // Ordering. A duplicate or out-of-order reply is discarded and a gap
  // invalidates delta state (protocol section 6.3).
  const EventSequenceTracker::Verdict verdict =
      endpoint->ClassifyEventSequence(snapshot.event_sequence);
  if (verdict == EventSequenceTracker::Verdict::kDuplicate ||
      verdict == EventSequenceTracker::Verdict::kOutOfOrder) {
    envelope.code = ObservationResultCode::kStalePageEpoch;
    FinishObservation(request_id, std::move(envelope));
    return;
  }

  const ObservationResultCode echo =
      ValidateSnapshotEcho(clamped, endpoint->page_epoch(), snapshot);
  if (echo != ObservationResultCode::kOk) {
    envelope.code = echo;
    FinishObservation(request_id, std::move(envelope));
    return;
  }

  BrowserOwnedObservationFacts facts = CollectBrowserOwnedFacts(clamped);
  const FrameInclusionInputs frame_inputs =
      BuildFrameInclusionInputs(clamped, grant, facts.origin);
  FillObservationEnvelope(clamped, snapshot, std::move(facts), frame_inputs,
                          encoder_.get(), ObservationResultCode::kOk,
                          &envelope);

  // A fresh snapshot is the only way out of an awaiting-resnapshot
  // subscription, so a successful one re-establishes any stream on this frame.
  //
  // The three admitted codes are the ones that carry a snapshot. A page whose
  // own structured data disagrees with its own text produces `kConflicted`
  // every time it is read, so treating that as no snapshot left the
  // subscription awaiting a resnapshot that could never arrive — on that page,
  // for as long as the task looked at it (decision 0207).
  if (envelope.code == ObservationResultCode::kOk ||
      envelope.code == ObservationResultCode::kIncomplete ||
      envelope.code == ObservationResultCode::kConflicted) {
    // And it is the only way out of the endpoint's own resnapshot requirement,
    // for the same reason. This is where the browser learns what the
    // renderer's revision numbering has reached; it keeps none of its own.
    endpoint->NoteSnapshotObserved(envelope.graph_revision,
                                   envelope.event_sequence);
    deltas_.RebaseAfterSnapshot(clamped.root_frame_id, envelope.page_epoch,
                                envelope.graph_revision,
                                envelope.event_sequence);
  }
  FinishObservation(request_id, std::move(envelope));
}

// --- terminal results -------------------------------------------------------


void PageIntelligenceServiceImpl::RecordObservationOutcome(
    const PendingObservation& pending,
    const ObservationEnvelope& envelope) {
  ObservationRecord record;
  record.request_id = ToRecordIdentifier(envelope.request_id.value);
  if (!PopulateObservationRecordAuthority(pending.authority_subject, &record)) {
    return;
  }
  record.tab_id = ToRecordIdentifier(envelope.tab_id.value);
  record.frame_id = ToRecordIdentifier(envelope.root_frame_id.value);
  record.page_epoch = ToRecordIdentifier(envelope.page_epoch.value);
  record.graph_revision = envelope.graph_revision;
  record.code = envelope.code;
  record.node_count = envelope.node_count;
  record.byte_count = envelope.total_bytes;
  record.truncated = envelope.truncation.truncated;
  record.truncated_node_count = envelope.truncation.omitted_node_count;
  record.redacted_field_count = envelope.redaction.redacted_field_count;
  record.suppressed_secret_value_count =
      envelope.redaction.suppressed_secret_value_count;
  record.adapters_requested =
      static_cast<uint32_t>(pending.clamped.adapters.size());
  for (const AdapterReport& report : envelope.adapters) {
    if (report.status != AdapterStatus::kOk) {
      ++record.adapters_unavailable;
    }
  }
  record.cancelled = envelope.code == ObservationResultCode::kCancelled;
  record.origin_category = envelope.origin.kind == OriginKind::kOpaque
                               ? OriginCategory::kOpaque
                               : OriginCategory::kUnknown;
  record.latency_us =
      (base::TimeTicks::Now() - pending.started_at).InMicroseconds();
  observability_.Record(record);
}

void PageIntelligenceServiceImpl::FinishObservation(
    RequestId request_id,
    ObservationEnvelope envelope) {
  auto it = pending_observations_.find(request_id);
  if (it != pending_observations_.end()) {
    PendingObservation pending = std::move(it->second);
    pending_observations_.erase(it);
    // Claiming the guard stops the deadline from producing a second terminal
    // result for a request the reply already settled.
    if (pending.deadline) {
      pending.deadline->Claim();
    }
    RecordObservationOutcome(pending, envelope);
  }
  if (sink_) {
    sink_->OnObservationResult(std::move(envelope));
  }
}

}  // namespace taffy
