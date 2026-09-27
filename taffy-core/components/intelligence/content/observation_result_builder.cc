// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/observation_result_builder.h"

#include <utility>

#include "base/check.h"
#include "base/numerics/clamped_math.h"
#include "taffy/components/intelligence/content/bip_graph_payload.h"
#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/components/intelligence/content/observed_link_collection.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"

namespace taffy {

namespace {

// Records that a budget was reached, without duplicating an entry a renderer
// already named. Which budget was reached is the whole content of a useful
// truncation report, so the list is built rather than summarised.
void NoteBudgetReached(TruncationSummary* truncation, BudgetKind budget) {
  for (BudgetKind existing : truncation->budgets_reached) {
    if (existing == budget) {
      return;
    }
  }
  truncation->budgets_reached.push_back(budget);
  truncation->truncated = true;
}

}  // namespace

ObservationResultCode ValidateSnapshotEcho(
    const ObservationRequest& clamped,
    const PageEpoch& live_epoch,
    const mojom::PageSnapshot& snapshot) {
  // Identity echo. The renderer was told these values; a reply that echoes
  // anything else belongs to a document the broker did not ask about. Checked
  // rather than adopted: nothing below reads the echo again.
  if (snapshot.tab_id != clamped.tab_id.value ||
      snapshot.root_frame_id != clamped.root_frame_id.value) {
    return ObservationResultCode::kStalePageEpoch;
  }
  if (snapshot.page_epoch != live_epoch.value) {
    return ObservationResultCode::kStalePageEpoch;
  }
  if (snapshot.scope != ToMojom(clamped.scope)) {
    return ObservationResultCode::kInternalError;
  }
  const bool echoed_form_root = !snapshot.form_root.is_null();
  if (echoed_form_root != clamped.form_root.has_value()) {
    return ObservationResultCode::kInternalError;
  }
  if (clamped.form_root.has_value() &&
      (snapshot.form_root->node_id != clamped.form_root->node_id.value ||
       snapshot.form_root->minimum_graph_revision !=
           clamped.form_root->minimum_graph_revision)) {
    return ObservationResultCode::kInternalError;
  }
  const bool echoed_media_root = !snapshot.media_root.is_null();
  if (echoed_media_root != clamped.media_root.has_value()) {
    return ObservationResultCode::kInternalError;
  }
  if (clamped.media_root.has_value() &&
      (snapshot.media_root->node_id != clamped.media_root->node_id.value ||
       snapshot.media_root->minimum_graph_revision !=
           clamped.media_root->minimum_graph_revision)) {
    return ObservationResultCode::kInternalError;
  }

  // Budget. The renderer was given the clamped budget; exceeding it is a
  // protocol violation, not a bigger snapshot to accept gratefully. Refused
  // outright rather than truncated here, because truncating a reply the
  // browser did not build would mean choosing which half of an untrusted
  // structure to keep.
  if (clamped.budget.max_nodes != 0 &&
      snapshot.nodes.size() > clamped.budget.max_nodes) {
    return ObservationResultCode::kBudgetExceeded;
  }
  if (clamped.budget.max_frames != 0 &&
      snapshot.frames.size() > clamped.budget.max_frames) {
    return ObservationResultCode::kBudgetExceeded;
  }
  return ObservationResultCode::kOk;
}

void FillObservationEnvelope(const ObservationRequest& clamped,
                             const mojom::PageSnapshot& snapshot,
                             BrowserOwnedObservationFacts facts,
                             const FrameInclusionInputs& frame_inputs,
                             GraphPayloadEncoder* encoder,
                             ObservationResultCode base_code,
                             ObservationEnvelope* envelope) {
  CHECK(envelope);
  CHECK(encoder);

  // Everything the browser's own redaction pass removes while building this
  // envelope, from whichever renderer-authored string it removed it from. The
  // pass runs regardless; the tally is only how the removal becomes a reported
  // fact rather than a silent repair.
  RescanTally rescan;

  envelope->snapshot_id = SnapshotId{snapshot.snapshot_id};
  envelope->graph_revision = snapshot.graph_revision;
  envelope->event_sequence = snapshot.event_sequence;
  envelope->node_count = static_cast<uint32_t>(snapshot.nodes.size());
  envelope->scope = clamped.scope;
  envelope->form_root = clamped.form_root;
  envelope->media_root = clamped.media_root;

  // The renderer's lifecycle claim is read, but the browser's frame tree is
  // what any consumer actually acts on: an unmapped value fails closed to
  // destroyed rather than defaulting to active.
  envelope->lifecycle_state = FromMojom(snapshot.lifecycle_state)
                                  .value_or(DocumentLifecycleState::kDestroyed);

  if (snapshot.truncation) {
    envelope->truncation = FromMojom(*snapshot.truncation);
  }
  if (snapshot.redaction_summary) {
    envelope->redaction.redacted_field_count =
        snapshot.redaction_summary->redacted_field_count;
    envelope->redaction.suppressed_secret_value_count =
        snapshot.redaction_summary->suppressed_secret_value_count;
    envelope->redaction.sensitive_zone_count =
        snapshot.redaction_summary->sensitive_zone_count;
    envelope->redaction.policy_filtered_frame_count =
        snapshot.redaction_summary->policy_filtered_frame_count;
  }
  for (const mojom::AdapterReportPtr& report : snapshot.adapters) {
    if (!report) {
      continue;
    }
    AdapterReport out;
    // An adapter this build cannot name is dropped rather than coerced to a
    // neighbour, and the drop shows up as a missing adapter in the requirement
    // check below — which is the honest consequence (protocol section 6.2).
    const std::optional<AdapterKind> adapter = FromMojom(report->adapter);
    if (!adapter.has_value()) {
      continue;
    }
    out.adapter = *adapter;
    out.status = static_cast<AdapterStatus>(report->status);
    out.adapter_version = report->adapter_version;
    out.extraction_rule_version = report->extraction_rule_version;
    // The contract says a detail code is a stable diagnostic code and never
    // page content. A compromised renderer is not bound by what the contract
    // says, and this string is carried onward, so it is rescanned for the same
    // reason a node name is: the other free-text field in this envelope.
    if (report->detail_code.has_value()) {
      out.detail_code = RescanRendererText(*report->detail_code, &rescan);
    }
    envelope->adapters.push_back(std::move(out));
  }
  for (const mojom::SnapshotWarningPtr& warning : snapshot.warnings) {
    if (warning) {
      envelope->warning_codes.push_back(static_cast<uint8_t>(warning->code));
    }
  }

  // Browser-owned facts overwrite whatever the renderer echoed.
  envelope->profile_id = std::move(facts.profile_id);
  envelope->browser_window_id = std::move(facts.browser_window_id);
  envelope->page_epoch = std::move(facts.page_epoch);
  envelope->origin = std::move(facts.origin);
  envelope->committed_url_metadata = std::move(facts.committed_url_metadata);
  envelope->is_potentially_trustworthy = facts.is_potentially_trustworthy;
  envelope->is_incognito = facts.is_incognito;
  envelope->frames = std::move(facts.frames);

  // Frame eligibility. A cross-origin child frame enters a task because the
  // grant named its origin, never because it happened to be in the tree
  // (protocol section 8.1, [Open (OD-045)]).
  const FrameInclusionSummary frames =
      ApplyFrameInclusion(frame_inputs, &envelope->frames);
  envelope->redaction.policy_filtered_frame_count +=
      frames.policy_filtered_count;
  if (frames.budget_filtered_count > 0) {
    NoteBudgetReached(&envelope->truncation, BudgetKind::kMaxFrames);
    envelope->truncation.omitted_frame_count += frames.budget_filtered_count;
    // Frames the budget cut could have carried the answer, which is exactly
    // what this flag is for.
    envelope->truncation.may_change_answer = true;
  }

  // The raw graph crosses only as opaque bytes. The encoder also derives the
  // bounded, redacted UI projection during the same pass, but assigns fresh
  // identities and never interprets page meaning. Raw parsing remains in Rust.
  // The frame identity handed to the encoder is the broker's, not the
  // snapshot's. ValidateSnapshotEcho has already refused a reply whose echoed
  // root frame is not this one, so the two agree here — but the encoder is
  // given the value that agreement was measured against, so that a node row
  // carries an identity the browser issued rather than one it merely
  // recognised.
  GraphPayloadEncoder::Encoded encoded = encoder->Encode(
      snapshot, clamped.root_frame_id, clamped.budget.max_total_bytes,
      clamped.budget.max_text_bytes);
  envelope->encoding = encoded.encoding;
  envelope->graph_payload = std::move(encoded.bytes);
  envelope->inspector_projection = std::move(encoded.inspector_projection);
  envelope->total_bytes = static_cast<uint32_t>(envelope->graph_payload.size());
  const bool observed_links_valid = CollectTransientObservedLinks(
      clamped, snapshot, &envelope->transient_observed_links);
  rescan.Add(encoded.rescan);
  if (encoded.rejected || !observed_links_valid) {
    // A malformed, over-ceiling, or authority-invalid renderer graph is not a
    // successful metadata-only observation. The encoder gives all three the
    // same fail-closed answer and retains none of the partial bytes, so the
    // transport result must say the observation failed too.
    //
    // Which failure, though, is not the same question. A graph that would not
    // fit under the byte ceiling is the budget doing its job on a page that is
    // genuinely too large, and the caller's next move is to ask for less; a
    // malformed or authority-invalid reply is a renderer that is not speaking
    // the protocol, and there is nothing the caller can ask for instead. Both
    // reported kInternalError until 2026-09-07, which is how
    // `HiddenContentTest.AnOversizedDocumentTruncatesExplicitly` came to see a
    // correctly truncated observation of a deliberately oversized document
    // arrive with the code that means the browser broke.
    base_code = CombineObservationCodes(
        base_code,
        encoded.rejected && encoded.did_not_fit && observed_links_valid
            ? ObservationResultCode::kBudgetExceeded
            : ObservationResultCode::kInternalError);
  }

  // A secret this process removed is a suppressed secret value, whichever
  // layer removed it, so it is reported in the field that already means that
  // rather than in a second one a consumer would have to learn. A non-zero
  // contribution from here always means the renderer's layer did not do its
  // job — the browser's pass is the backstop, not the intended first cut.
  envelope->redaction.suppressed_secret_value_count =
      base::ClampAdd(envelope->redaction.suppressed_secret_value_count,
                     rescan.redacted_span_count);

  // Naming the budget that was reached. The renderer reports what it hit while
  // extracting; the browser adds what it can see from the counts, so a
  // truncation is never reported without a named cause.
  if (envelope->truncation.truncated &&
      envelope->truncation.budgets_reached.empty()) {
    if (clamped.budget.max_nodes != 0 &&
        envelope->node_count >= clamped.budget.max_nodes) {
      NoteBudgetReached(&envelope->truncation, BudgetKind::kMaxNodes);
    }
    if (clamped.budget.max_total_bytes != 0 &&
        envelope->total_bytes >= clamped.budget.max_total_bytes) {
      NoteBudgetReached(&envelope->truncation, BudgetKind::kMaxTotalBytes);
    }
    if (envelope->truncation.budgets_reached.empty()) {
      // Truncated for a reason the renderer did not name and the browser
      // cannot infer. Recording the message ceiling is honest — it is the
      // bound that always applies — and leaving the list empty would be a
      // truncation report that says nothing.
      NoteBudgetReached(&envelope->truncation, BudgetKind::kMaxMessageBytes);
    }
  }

  const AdapterRequirementOutcome adapters = CheckAdapterReports(
      clamped.adapters, envelope->adapters, envelope->truncation.truncated);
  for (uint8_t warning : adapters.warning_codes) {
    envelope->warning_codes.push_back(warning);
  }

  ObservationResultCode code =
      CombineObservationCodes(base_code, adapters.code);
  if (envelope->truncation.truncated) {
    // A truncated result is never complete, whatever else was fine.
    code = CombineObservationCodes(code, ObservationResultCode::kIncomplete);
  }
  envelope->code = code;
  // A link survives a reading that happened. Its address was read from the
  // node it names, normalized here, and bound to that node's own identity; a
  // budget that omitted ninety other nodes says nothing about it, and an
  // omitted node cannot forge an identifier that was present. Erasing the
  // table on any truncation meant that on every page large enough to reach a
  // budget — which is every page a person sends an errand to — the table was
  // empty, so `browser.link.open` had no link to open and a model that
  // wanted to follow a result fell back to a synthetic click and was refused.
  //
  // The three admitted codes are the ones that mean the reading happened and
  // carried a caveat: nothing wrong, adapters that disagreed about a role,
  // and a reading that stopped early. Everything above them says the
  // observation did not happen or does not describe this document, and those
  // still take the table with them. `ObservedLinkRegistry::Replace` states
  // the same rule against the envelope it receives, and both copies are
  // load-bearing: this one decides what crosses, that one decides what is
  // registered, and they live in components that may not include each other.
  if ((code != ObservationResultCode::kOk &&
       code != ObservationResultCode::kConflicted &&
       code != ObservationResultCode::kIncomplete) ||
      envelope->encoding != GraphPayloadEncoding::kBipContract) {
    envelope->transient_observed_links.clear();
  }
}

}  // namespace taffy
