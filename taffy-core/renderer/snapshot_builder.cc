// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_builder.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/snapshot_adapter_collection.h"
#include "taffy/renderer/snapshot_form_observation_root.h"
#include "taffy/renderer/snapshot_graph_projection.h"
#include "taffy/renderer/wire_conversions.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "url/gurl.h"

// VERIFY AT SP-04:
//   * blink::WebAXContext construction and HasActiveDocument() - used here
//     only to answer "can an accessibility tree exist for this document",
//     which is a capability question rather than an extraction. If building a
//     context is expensive even when the answer is no, this probe has to move
//     behind the first accessibility run and the capability report becomes
//     "unknown until asked", which is a worse contract but an honest one.
//   * blink::WebLocalFrame::FrameWidget() reachability from a child local
//     frame. See layout_visibility_adapter.cc for what depends on it.
//   * blink::WebDocument::Forms(), GetElementsByHTMLTagName(), and
//     WebElement::DynamicTo<WebFormControlElement>() spellings.

namespace taffy {

namespace {

// The renderer's view of its own origin. Deliberately incomplete for an
// opaque origin: every opaque origin serializes to the same token, and this
// process must not mint the session-local identifier that distinguishes
// them. The broker replaces this from its own record before any consumer
// sees it (protocol section 7.2); it is here so that a disagreement between
// the two is visible in diagnostics.
mojom::OriginPtr RendererOrigin(const blink::WebSecurityOrigin& origin) {
  auto out = mojom::Origin::New();
  if (origin.IsNull() || origin.IsOpaque()) {
    out->kind = mojom::OriginKind::kOpaque;
    return out;
  }
  out->kind = mojom::OriginKind::kTuple;
  out->serialization = origin.ToString().Utf8();
  return out;
}

}  // namespace

SnapshotBuilder::Input::Input() = default;
SnapshotBuilder::Input::~Input() = default;

SnapshotBuilder::Output::Output() = default;
SnapshotBuilder::Output::Output(Output&&) = default;
SnapshotBuilder::Output& SnapshotBuilder::Output::operator=(Output&&) = default;
SnapshotBuilder::Output::~Output() = default;

// static
SnapshotBuilder::Output SnapshotBuilder::Build(const Input& input,
                                               PageCapabilities capabilities) {
  Output output;
  output.capabilities = std::move(capabilities);

  // The browser owns the lifecycle (protocol section 5.5). Without its
  // statement there is no document this builder may describe, and describing
  // one anyway would put a renderer's guess where a browser fact belongs.
  if (!input.lifecycle_state.has_value()) {
    output.code = mojom::ObservationResultCode::kDocumentInactive;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code = "browser-stated-no-lifecycle";
    output.warnings.push_back(std::move(warning));
    return output;
  }

  const mojom::SnapshotRequest& request = *input.request;
  const ExtractionScope scope = wire::FromMojom(request.scope);

  const FormObservationRootStatus initial_form_root =
      ValidateFormObservationRoot(request, input.frame, *input.store,
                                  ObservationRootValidationPhase::kAdmission);
  if (initial_form_root != FormObservationRootStatus::kOk) {
    output.code = mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code =
        std::string(FormObservationRootDetailCode(initial_form_root));
    output.warnings.push_back(std::move(warning));
    return output;
  }
  const MediaObservationRootStatus initial_media_root =
      ValidateMediaObservationRoot(request, input.frame, *input.store,
                                   ObservationRootValidationPhase::kAdmission);
  if (initial_media_root != MediaObservationRootStatus::kOk) {
    output.code = mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code =
        std::string(MediaObservationRootDetailCode(initial_media_root));
    output.warnings.push_back(std::move(warning));
    return output;
  }

  // A scope this document cannot serve is UNSUPPORTED before anything runs.
  // Protocol section 6.2: never a degraded result that looks complete.
  if (!output.capabilities.SupportsScope(scope)) {
    output.code = mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code = "scope-not-supported-for-this-document";
    output.warnings.push_back(std::move(warning));
    return output;
  }

  // A required adapter this document cannot support is also UNSUPPORTED, and
  // for the same reason. Checked before any work so a caller that asked for
  // something impossible is told immediately rather than after a full walk.
  for (const mojom::AdapterRequirementPtr& requirement : request.adapters) {
    if (requirement->requirement != mojom::AdapterRequirementLevel::kRequired) {
      continue;
    }
    const std::optional<AdapterKind> kind =
        wire::FromMojom(requirement->adapter);
    if (!kind.has_value() ||
        output.capabilities.For(kind.value()).availability ==
            AdapterAvailability::kUnsupported) {
      output.code = mojom::ObservationResultCode::kUnsupported;
      auto warning = mojom::SnapshotWarning::New();
      warning->code = mojom::WarningCode::kAdapterUnavailable;
      warning->detail_code = "required-adapter-unsupported";
      output.warnings.push_back(std::move(warning));
      return output;
    }
  }

  // The renderer narrows the browser's already-narrowed budget again. The two
  // narrowings answer different questions: the browser's protects policy,
  // this one protects this process, and neither is redundant.
  RequestedSnapshotBudget requested;
  requested.max_nodes = request.max_nodes;
  requested.max_text_bytes = request.max_text_bytes;
  requested.max_total_bytes = request.max_total_bytes;
  requested.max_depth = request.max_depth;
  requested.deadline_ms = request.deadline_ms;
  const ObservationLimits limits =
      ObservationLimits::ProcessSafeCeiling().NarrowedTo(requested);

  BudgetLedger ledger(limits.snapshot());

  auto snapshot = mojom::PageSnapshot::New();
  SnapshotAdapterCollection collected = CollectSnapshotAdapters(
      input.frame, input.store, input.browser_facts, request, scope, limits,
      &ledger, std::move(output.capabilities));
  output.capabilities = std::move(collected.capabilities);
  output.warnings = std::move(collected.warnings);
  snapshot->adapters = std::move(collected.reports);
  if (collected.refused) {
    output.code = collected.refusal_code;
    return output;
  }
  bool any_incomplete = collected.incomplete;
  const bool any_conflicted = collected.conflicted;
  const bool dom_stopped_at_shadow_boundary =
      collected.dom_stopped_at_shadow_boundary;
  const std::set<SemanticNodeId> selection_node_ids =
      std::move(collected.selection_node_ids);
  ExtractedGraph graph = std::move(collected.graph);

  // A root may be removed or replaced while adapters run. Validate the same
  // issued identity again after the walk; never project a different element
  // or broaden to the document. The caller's freshness floor was already
  // proven at admission. The final pass intentionally ignores that floor
  // because this collection may itself have added bounds or visibility facts
  // to the root and advanced its revision.
  const FormObservationRootStatus final_form_root = ValidateFormObservationRoot(
      request, input.frame, *input.store,
      ObservationRootValidationPhase::kAfterCollection);
  if (final_form_root != FormObservationRootStatus::kOk) {
    output.code = mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code =
        std::string(FormObservationRootDetailCode(final_form_root));
    output.warnings.push_back(std::move(warning));
    return output;
  }
  const MediaObservationRootStatus final_media_root =
      ValidateMediaObservationRoot(
          request, input.frame, *input.store,
          ObservationRootValidationPhase::kAfterCollection);
  if (final_media_root != MediaObservationRootStatus::kOk) {
    output.code = mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code =
        std::string(MediaObservationRootDetailCode(final_media_root));
    output.warnings.push_back(std::move(warning));
    return output;
  }

  std::optional<SemanticNodeId> section_root;
  if (request.form_root) {
    section_root = SemanticNodeId(request.form_root->node_id);
    const bool root_was_collected = std::ranges::any_of(
        graph.nodes, [&section_root](const SemanticNode& node) {
          return node.node_id == *section_root;
        });
    if (!root_was_collected) {
      output.code = ledger.exhausted()
                        ? mojom::ObservationResultCode::kBudgetExceeded
                        : mojom::ObservationResultCode::kUnsupported;
      auto warning = mojom::SnapshotWarning::New();
      warning->code = mojom::WarningCode::kAdapterUnavailable;
      warning->detail_code = "form-root-not-collected";
      output.warnings.push_back(std::move(warning));
      return output;
    }
  }
  std::optional<SemanticNodeId> media_root;
  if (request.media_root) {
    media_root = SemanticNodeId(request.media_root->node_id);
    const bool root_was_collected = std::ranges::any_of(
        graph.nodes, [&media_root](const SemanticNode& node) {
          return node.node_id == *media_root;
        });
    if (!root_was_collected) {
      output.code = ledger.exhausted()
                        ? mojom::ObservationResultCode::kBudgetExceeded
                        : mojom::ObservationResultCode::kUnsupported;
      auto warning = mojom::SnapshotWarning::New();
      warning->code = mojom::WarningCode::kAdapterUnavailable;
      warning->detail_code = "media-root-not-collected";
      output.warnings.push_back(std::move(warning));
      return output;
    }
  }
  if (scope == ExtractionScope::kSelection && selection_node_ids.empty()) {
    output.code = ledger.exhausted()
                      ? mojom::ObservationResultCode::kBudgetExceeded
                      : mojom::ObservationResultCode::kUnsupported;
    auto warning = mojom::SnapshotWarning::New();
    warning->code = mojom::WarningCode::kAdapterUnavailable;
    warning->detail_code = "selection-not-collected";
    output.warnings.push_back(std::move(warning));
    return output;
  }

  const SnapshotProjectionSummary projection =
      ProjectSnapshotGraph(scope, section_root, media_root, selection_node_ids,
                           graph, dom_stopped_at_shadow_boundary, &ledger,
                           snapshot.get(), &output.warnings);
  any_incomplete |= projection.incomplete;

  const TruncationReport& truncation = ledger.report();
  snapshot->truncation = mojom::Truncation::New();
  snapshot->truncation->truncated =
      truncation.truncated || projection.dropped_by_scope > 0;
  if (truncation.truncated) {
    snapshot->truncation->budgets_reached.push_back(
        wire::ToMojom(truncation.first_budget_reached));
  }
  snapshot->truncation->omitted_node_count =
      truncation.omitted_node_count + projection.dropped_by_scope;
  snapshot->truncation->omitted_text_bytes = truncation.omitted_text_bytes;
  snapshot->truncation->omitted_frame_count = 0;
  // A scope narrowing is not an omission that could change the answer: the
  // caller asked for exactly this. A budget one is.
  snapshot->truncation->may_change_answer =
      truncation.omitted_data_could_change_answer;

  snapshot->redaction_summary = mojom::RedactionSummary::New();
  snapshot->redaction_summary->applied_features = {
      mojom::RedactionFeature::kSecretValueSuppression,
      mojom::RedactionFeature::kSensitiveZoneClassification,
      mojom::RedactionFeature::kPatternDetectors,
      mojom::RedactionFeature::kContentTrustLabelling,
      mojom::RedactionFeature::kInjectionSignalDetection,
  };
  snapshot->redaction_summary->redacted_field_count =
      projection.redacted_fields;
  snapshot->redaction_summary->suppressed_secret_value_count =
      projection.suppressed_secret_values;
  snapshot->redaction_summary->sensitive_zone_count =
      projection.sensitive_zones;
  snapshot->redaction_summary->policy_filtered_frame_count = 0;

  // --- The envelope --------------------------------------------------------
  // Everything below is identity, and every value of it comes from the
  // request the broker authored or from a counter the endpoint owns. The two
  // renderer-observed fields - the security origin and whether the URL has a
  // query or a fragment - are diagnostics the broker overwrites from its own
  // committed navigation record (protocol section 7.2). They are here so a
  // disagreement between the two is visible, never so a consumer reads them.
  const blink::WebDocument document = input.frame->GetDocument();
  snapshot->schema_version = request.schema_version;
  snapshot->snapshot_id = input.snapshot_id;
  snapshot->request_id = request.request_id;
  snapshot->profile_id = request.profile_id;
  // The renderer does not know which browser window it is in, and inventing
  // one would be a renderer claiming browser-owned identity.
  snapshot->browser_window_id = std::string();
  snapshot->tab_id = request.tab_id;
  snapshot->root_frame_id = request.root_frame_id;
  snapshot->page_epoch = input.store->page_epoch().value();
  snapshot->graph_revision = input.store->current_revision().value();
  snapshot->event_sequence = input.event_sequence;
  snapshot->lifecycle_state = input.lifecycle_state.value();
  snapshot->scope = request.scope;
  snapshot->form_root = request.form_root.Clone();
  snapshot->media_root = request.media_root.Clone();
  snapshot->capture_time_monotonic_ms = static_cast<uint64_t>(
      (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds());

  snapshot->committed_url_metadata = mojom::UrlMetadata::New();
  snapshot->committed_url_metadata->origin =
      RendererOrigin(document.GetSecurityOrigin());
  snapshot->committed_url_metadata->disclosure =
      mojom::UrlDisclosure::kOriginOnly;
  const GURL document_url = document.Url();
  snapshot->committed_url_metadata->has_query = document_url.has_query();
  snapshot->committed_url_metadata->has_fragment = document_url.has_ref();

  snapshot->origin_metadata = mojom::OriginMetadata::New();
  snapshot->origin_metadata->origin =
      RendererOrigin(document.GetSecurityOrigin());
  snapshot->origin_metadata->is_potentially_trustworthy =
      document.IsSecureContext();
  // The renderer does not know whether it is in an incognito profile, and a
  // renderer-supplied answer would be worthless for a policy decision.
  snapshot->origin_metadata->is_incognito = false;

  auto frame_descriptor = mojom::FrameDescriptor::New();
  frame_descriptor->frame_id = request.root_frame_id;
  frame_descriptor->is_main_frame = !input.frame->Parent();
  frame_descriptor->is_out_of_process = false;
  frame_descriptor->is_cross_origin_to_parent =
      input.browser_facts.cross_origin_frame;
  frame_descriptor->origin_metadata = snapshot->origin_metadata.Clone();
  frame_descriptor->page_epoch = input.store->page_epoch().value();
  frame_descriptor->graph_revision = input.store->current_revision().value();
  frame_descriptor->lifecycle_state = input.lifecycle_state.value();
  frame_descriptor->included = true;
  snapshot->frames.push_back(std::move(frame_descriptor));

  // The warnings this build accumulated belong on the snapshot, not on the
  // reply that carries it. `warnings` is a required member of the contract's
  // snapshot object (taffy-core/contracts/bip/schema/snapshot.schema.json), and
  // it is where every consumer reads them from - the browser's envelope fill
  // walks PageSnapshot.warnings. SnapshotResult keeps its own list for the one
  // case the schema has no room for: a code with no snapshot to hang a reason
  // on.
  //
  // Moved rather than copied, so a warning appears exactly once on the wire
  // and the endpoint's copy into the reply is a no-op whenever a snapshot
  // exists. The vector is cleared explicitly because a moved-from vector is
  // valid but unspecified, and the endpoint iterates it straight after.
  snapshot->warnings = std::move(output.warnings);
  output.warnings.clear();

  output.snapshot = std::move(snapshot);
  output.code = any_conflicted ? mojom::ObservationResultCode::kConflicted
                : (any_incomplete || truncation.truncated)
                    ? mojom::ObservationResultCode::kIncomplete
                    : mojom::ObservationResultCode::kOk;
  return output;
}

}  // namespace taffy
