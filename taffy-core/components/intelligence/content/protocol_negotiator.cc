// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/protocol_negotiator.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/budget_clamp.h"
#include "taffy/components/intelligence/content/monotonic_clock.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"
#include "taffy/common/public/bip_action.h"

namespace taffy {

namespace {


// The stricter of the two ceilings, field by field. A caller planning a budget
// reads this rather than computing a minimum it might get wrong.
ProcessBudgetLimits EffectiveLimits(const ProcessBudgetLimits& endpoint,
                                    const ProcessBudgetLimits& process) {
  ProcessBudgetLimits effective;
  effective.max_message_bytes =
      std::min(endpoint.max_message_bytes, process.max_message_bytes);
  effective.max_nodes = std::min(endpoint.max_nodes, process.max_nodes);
  effective.max_text_bytes =
      std::min(endpoint.max_text_bytes, process.max_text_bytes);
  effective.max_total_bytes =
      std::min(endpoint.max_total_bytes, process.max_total_bytes);
  effective.max_depth = std::min(endpoint.max_depth, process.max_depth);
  effective.max_frames = std::min(endpoint.max_frames, process.max_frames);
  effective.max_delta_queue_depth =
      std::min(endpoint.max_delta_queue_depth, process.max_delta_queue_depth);
  effective.max_snapshot_deadline_ms = std::min(
      endpoint.max_snapshot_deadline_ms, process.max_snapshot_deadline_ms);
  // The one axis on which stricter means larger: an endpoint that will not
  // produce deltas faster than its own floor sets the floor.
  effective.min_delta_interval_ms =
      std::max(endpoint.min_delta_interval_ms, process.min_delta_interval_ms);
  return effective;
}

}  // namespace

ProtocolNegotiator::ProtocolNegotiator(CompletionCallback on_settled)
    : on_settled_(std::move(on_settled)) {
  CHECK(on_settled_);
}

ProtocolNegotiator::~ProtocolNegotiator() = default;

// static
ProtocolSupportEnvelope ProtocolNegotiator::BareEnvelope(
    const RequestId& request_id,
    const TabId& tab_id,
    ObservationResultCode code) {
  ProtocolSupportEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.request_id = request_id;
  envelope.tab_id = tab_id;
  envelope.code = code;
  envelope.observed_at_monotonic_ms = NowMonotonicMs();
  return envelope;
}

void ProtocolNegotiator::Query(
    const RequestId& request_id,
    const TabId& tab_id,
    const FrameId& frame_id,
    mojo::AssociatedRemote<mojom::PageIntelligence>& remote,
    base::TimeDelta deadline) {
  CHECK(request_id.is_valid());
  CHECK(!pending_.contains(request_id));

  Pending pending;
  pending.request_id = request_id;
  pending.tab_id = tab_id;
  pending.frame_id = frame_id;
  // Bounded, like every call that leaves this process. A renderer that never
  // answers still settles the request (protocol section 15).
  pending.deadline = RendererCallDeadline::Arm(
      deadline,
      base::BindOnce(&ProtocolNegotiator::CancelWithCode,
                     weak_factory_.GetWeakPtr(), request_id,
                     ObservationResultCode::kDeadlineExceeded));
  scoped_refptr<RendererCallDeadline> guard = pending.deadline;
  pending_.emplace(request_id, std::move(pending));

  remote->GetProtocolInfo(BindReplyWithDeadline(
      std::move(guard),
      base::BindOnce(&ProtocolNegotiator::OnProtocolInfo,
                     weak_factory_.GetWeakPtr(), request_id)));
}

bool ProtocolNegotiator::IsPending(const RequestId& request_id) const {
  return pending_.contains(request_id);
}

const ProtocolSupportEnvelope* ProtocolNegotiator::GetSupport(
    const FrameId& frame_id) const {
  auto it = support_.find(frame_id);
  return it == support_.end() ? nullptr : &it->second;
}

void ProtocolNegotiator::ForgetFrame(const FrameId& frame_id) {
  support_.erase(frame_id);
}

void ProtocolNegotiator::ForgetAll() {
  support_.clear();
}

void ProtocolNegotiator::CancelWithCode(const RequestId& request_id,
                                        ObservationResultCode code) {
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;  // Already terminal. Exactly one result per request id.
  }
  Settle(request_id, BareEnvelope(request_id, it->second.tab_id, code));
}

void ProtocolNegotiator::OnProtocolInfo(RequestId request_id,
                                        mojom::ProtocolInfoPtr info) {
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;  // Cancelled or timed out; a late reply is dropped.
  }
  const TabId tab_id = it->second.tab_id;
  const FrameId frame_id = it->second.frame_id;

  ProtocolSupportEnvelope envelope =
      BareEnvelope(request_id, tab_id, ObservationResultCode::kUnsupported);
  envelope.frame_id = frame_id;

  // An endpoint that cannot answer is unsupported, not assumed. There is no
  // fallback to "the defaults are probably fine" anywhere below.
  if (!info || info->protocol_version != kBipSchemaVersion || !info->limits) {
    Settle(request_id, std::move(envelope));
    return;
  }

  // The version was compared against this build's own constant above, so what
  // is stored is that literal and there is nothing in it to scan. The
  // implementation identifier is the opposite: a free string the endpoint
  // chose, carried onward for diagnostics and read by nothing, which makes it
  // the third place a compromised renderer could volunteer a secret. It goes
  // through the browser's own pass for the same reason a node name does.
  //
  // No tally: a protocol-support envelope carries no redaction summary, so
  // there is nowhere here to report a count. The removal is the property that
  // matters and it is not conditional on being reportable.
  envelope.endpoint_protocol_version = info->protocol_version;
  envelope.implementation_id =
      RescanRendererText(info->implementation_id, nullptr);
  envelope.endpoint_limits = FromMojom(*info->limits);
  envelope.effective_limits =
      EffectiveLimits(envelope.endpoint_limits, GetProcessBudgetLimits());

  for (mojom::AdapterKind adapter : info->supported_adapters) {
    const std::optional<AdapterKind> mapped = FromMojom(adapter);
    if (!mapped.has_value()) {
      ++envelope.unmapped_adapter_count;
      continue;
    }
    AdapterSupport support;
    support.adapter = *mapped;
    support.supported = true;
    envelope.adapters.push_back(support);
  }
  for (mojom::ObservationScope scope : info->supported_scopes) {
    const std::optional<ObservationScope> mapped = FromMojom(scope);
    if (!mapped.has_value()) {
      ++envelope.unmapped_scope_count;
      continue;
    }
    envelope.supported_scopes.push_back(*mapped);
  }
  for (mojom::ActionType action : info->supported_action_types) {
    const std::optional<ActionType> mapped = FromMojom(action);
    if (!mapped.has_value()) {
      ++envelope.unmapped_action_type_count;
      continue;
    }
    envelope.supported_action_types.push_back(*mapped);
  }

  envelope.code = ObservationResultCode::kOk;
  support_.insert_or_assign(frame_id, envelope);
  Settle(request_id, std::move(envelope));
}

void ProtocolNegotiator::Settle(const RequestId& request_id,
                                ProtocolSupportEnvelope envelope) {
  auto it = pending_.find(request_id);
  if (it != pending_.end()) {
    if (it->second.deadline) {
      it->second.deadline->Claim();
    }
    pending_.erase(it);
  }
  on_settled_.Run(std::move(envelope));
}

}  // namespace taffy
