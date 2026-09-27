// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/fuzz/wire_seed_builder.h"

#include <utility>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// VERIFY AT SP-04: every generated mojom struct exposes a static
// Serialize(StructPtr*) returning std::vector<uint8_t> and a matching
// Deserialize(const void*, size_t, StructPtr*). The fuzzers in this
// directory depend on the second and this file on the first. Upstream
// file to read: mojo/public/tools/bindings/generators/cpp_templates/
// struct_declaration.tmpl. If the helpers are spelled differently at the
// pinned milestone, they change together and nothing else here does.

namespace taffy::test {

namespace {

std::string Text(const base::DictValue& dict, const char* key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

uint64_t Number(const base::DictValue& dict, const char* key) {
  if (std::optional<double> value = dict.FindDouble(key)) {
    return static_cast<uint64_t>(*value);
  }
  if (std::optional<int> value = dict.FindInt(key)) {
    return static_cast<uint64_t>(*value);
  }
  return 0;
}

mojom::OriginPtr Origin(const base::DictValue* dict) {
  auto origin = mojom::Origin::New();
  origin->kind = mojom::OriginKind::kTuple;
  origin->serialization =
      dict ? Text(*dict, "serialization") : std::string("http://seed.invalid");
  if (origin->serialization->empty()) {
    origin->kind = mojom::OriginKind::kOpaque;
    origin->serialization.reset();
    origin->opaque_id = "seed-opaque";
  }
  return origin;
}

mojom::UrlMetadataPtr UrlMetadata(const base::DictValue* dict) {
  auto metadata = mojom::UrlMetadata::New();
  metadata->origin =
      Origin(dict ? dict->FindDict("origin") : nullptr);
  metadata->disclosure = mojom::UrlDisclosure::kOriginOnly;
  metadata->has_query = dict && dict->FindBool("has_query").value_or(false);
  metadata->has_fragment =
      dict && dict->FindBool("has_fragment").value_or(false);
  return metadata;
}

mojom::OriginMetadataPtr OriginMetadata(const base::DictValue* dict) {
  auto metadata = mojom::OriginMetadata::New();
  metadata->origin = Origin(dict ? dict->FindDict("origin") : nullptr);
  metadata->is_potentially_trustworthy =
      dict && dict->FindBool("is_potentially_trustworthy").value_or(false);
  metadata->is_incognito =
      dict && dict->FindBool("is_incognito").value_or(false);
  return metadata;
}

mojom::TruncationPtr Truncation(const base::DictValue* dict) {
  auto truncation = mojom::Truncation::New();
  truncation->truncated = dict && dict->FindBool("truncated").value_or(false);
  truncation->omitted_node_count =
      dict ? static_cast<uint32_t>(Number(*dict, "omitted_node_count")) : 0;
  truncation->omitted_text_bytes =
      dict ? static_cast<uint32_t>(Number(*dict, "omitted_text_bytes")) : 0;
  truncation->omitted_frame_count =
      dict ? static_cast<uint32_t>(Number(*dict, "omitted_frame_count")) : 0;
  truncation->may_change_answer =
      dict && dict->FindBool("may_change_answer").value_or(false);
  return truncation;
}

// One node per node in the golden document, carrying the fields a decoder has
// to walk. The semantic detail is deliberately shallow: a seed's job is to give
// the mutator a structurally valid starting point, not to be a second copy of
// the contract.
mojom::SemanticNodePtr Node(const base::DictValue& dict,
                            const std::string& fallback_frame_id) {
  auto node = mojom::SemanticNode::New();
  node->node_id = Text(dict, "node_id");
  node->frame_id = Text(dict, "frame_id");
  if (node->frame_id.empty()) {
    node->frame_id = fallback_frame_id;
  }
  node->role = mojom::SemanticRole::kUnknownContent;
  if (const std::string* name = dict.FindString("name")) {
    node->name = *name;
  }
  if (const base::ListValue* runs = dict.FindList("text_runs")) {
    for (const base::Value& value : *runs) {
      const base::DictValue* run_dict = value.GetIfDict();
      if (!run_dict) {
        continue;
      }
      auto run = mojom::TextRun::New();
      run->text = Text(*run_dict, "text");
      run->source_kind = mojom::SourceKind::kDom;
      run->sensitivity = mojom::Sensitivity::kNotSensitive;
      run->truncated = run_dict->FindBool("truncated").value_or(false);
      node->text_runs.push_back(std::move(run));
    }
  }
  node->sensitivity = mojom::Sensitivity::kNotSensitive;
  node->sources.push_back(mojom::SourceKind::kDom);
  node->confidence = 1.0;
  return node;
}

std::vector<uint8_t> BuildProtocolInfo(const base::DictValue& document) {
  auto info = mojom::ProtocolInfo::New();
  info->protocol_version = Text(document, "protocol_version");
  info->implementation_id = Text(document, "implementation_id");
  info->supported_adapters = {mojom::AdapterKind::kDom,
                              mojom::AdapterKind::kAccessibility};
  info->supported_scopes = {mojom::ObservationScope::kDocument};
  info->supported_action_types = {mojom::ActionType::kActivate};
  info->supported_redaction_features = {
      mojom::RedactionFeature::kSecretValueSuppression};

  const base::DictValue* limits_dict = document.FindDict("limits");
  auto limits = mojom::ProtocolLimits::New();
  limits->max_message_bytes =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_message_bytes")) : 0;
  limits->max_nodes =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_nodes")) : 0;
  limits->max_text_bytes =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_text_bytes")) : 0;
  limits->max_total_bytes =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_total_bytes")) : 0;
  limits->max_depth =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_depth")) : 0;
  limits->max_frames =
      limits_dict ? static_cast<uint32_t>(Number(*limits_dict, "max_frames")) : 0;
  limits->max_delta_queue_depth =
      limits_dict
          ? static_cast<uint32_t>(Number(*limits_dict, "max_delta_queue_depth"))
          : 0;
  limits->max_snapshot_deadline_ms =
      limits_dict
          ? static_cast<uint32_t>(Number(*limits_dict, "max_snapshot_deadline_ms"))
          : 0;
  limits->min_delta_interval_ms =
      limits_dict
          ? static_cast<uint32_t>(Number(*limits_dict, "min_delta_interval_ms"))
          : 0;
  info->limits = std::move(limits);
  return mojom::ProtocolInfo::Serialize(&info);
}

std::vector<uint8_t> BuildPageSnapshot(const base::DictValue& document) {
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->schema_version = Text(document, "schema_version");
  snapshot->snapshot_id = Text(document, "snapshot_id");
  snapshot->request_id = Text(document, "request_id");
  snapshot->profile_id = Text(document, "profile_id");
  snapshot->browser_window_id = Text(document, "browser_window_id");
  snapshot->tab_id = Text(document, "tab_id");
  snapshot->root_frame_id = Text(document, "root_frame_id");
  snapshot->page_epoch = Text(document, "page_epoch");
  snapshot->graph_revision = Number(document, "graph_revision");
  snapshot->event_sequence = Number(document, "event_sequence");
  snapshot->lifecycle_state = mojom::DocumentLifecycleState::kActive;
  snapshot->committed_url_metadata =
      UrlMetadata(document.FindDict("committed_url_metadata"));
  snapshot->origin_metadata = OriginMetadata(document.FindDict("origin_metadata"));
  snapshot->capture_time_monotonic_ms =
      Number(document, "capture_time_monotonic_ms");
  snapshot->scope = mojom::ObservationScope::kDocument;

  auto report = mojom::AdapterReport::New();
  report->adapter = mojom::AdapterKind::kDom;
  report->status = mojom::AdapterStatus::kOk;
  report->adapter_version = 1;
  report->extraction_rule_version = 1;
  snapshot->adapters.push_back(std::move(report));

  if (const base::ListValue* nodes = document.FindList("nodes")) {
    for (const base::Value& value : *nodes) {
      if (const base::DictValue* node = value.GetIfDict()) {
        snapshot->nodes.push_back(Node(*node, snapshot->root_frame_id));
      }
    }
  }

  if (const base::ListValue* frames = document.FindList("frames")) {
    for (const base::Value& value : *frames) {
      const base::DictValue* frame_dict = value.GetIfDict();
      if (!frame_dict) {
        continue;
      }
      auto frame = mojom::FrameDescriptor::New();
      frame->frame_id = Text(*frame_dict, "frame_id");
      if (const std::string* parent = frame_dict->FindString("parent_frame_id")) {
        frame->parent_frame_id = *parent;
      }
      frame->is_main_frame =
          frame_dict->FindBool("is_main_frame").value_or(false);
      frame->is_out_of_process =
          frame_dict->FindBool("is_out_of_process").value_or(false);
      frame->is_cross_origin_to_parent =
          frame_dict->FindBool("is_cross_origin_to_parent").value_or(false);
      frame->origin_metadata =
          OriginMetadata(frame_dict->FindDict("origin_metadata"));
      frame->page_epoch = Text(*frame_dict, "page_epoch");
      frame->graph_revision = Number(*frame_dict, "graph_revision");
      frame->lifecycle_state = mojom::DocumentLifecycleState::kActive;
      frame->included = frame_dict->FindBool("included").value_or(true);
      snapshot->frames.push_back(std::move(frame));
    }
  }
  if (snapshot->frames.empty()) {
    auto frame = mojom::FrameDescriptor::New();
    frame->frame_id = snapshot->root_frame_id;
    frame->is_main_frame = true;
    frame->origin_metadata = OriginMetadata(nullptr);
    frame->page_epoch = snapshot->page_epoch;
    frame->graph_revision = snapshot->graph_revision;
    frame->lifecycle_state = mojom::DocumentLifecycleState::kActive;
    frame->included = true;
    snapshot->frames.push_back(std::move(frame));
  }

  snapshot->truncation = Truncation(document.FindDict("truncation"));
  auto redaction = mojom::RedactionSummary::New();
  snapshot->redaction_summary = std::move(redaction);

  // The browser receives a SnapshotResult, never a bare snapshot, so the seed
  // is the message the boundary actually carries.
  auto result = mojom::SnapshotResult::New();
  result->request_id = snapshot->request_id;
  result->code = mojom::ObservationResultCode::kOk;
  result->snapshot = std::move(snapshot);
  return mojom::SnapshotResult::Serialize(&result);
}

std::vector<uint8_t> BuildPageDelta(const base::DictValue& document) {
  auto delta = mojom::PageDelta::New();
  delta->schema_version = Text(document, "schema_version");
  delta->subscription_id = Text(document, "subscription_id");
  delta->tab_id = Text(document, "tab_id");
  delta->frame_id = Text(document, "frame_id");
  delta->page_epoch = Text(document, "page_epoch");
  delta->from_revision = Number(document, "from_revision");
  delta->to_revision = Number(document, "to_revision");
  delta->event_sequence = Number(document, "event_sequence");
  delta->coalesced_mutation_count =
      static_cast<uint32_t>(Number(document, "coalesced_mutation_count"));

  if (const base::ListValue* changed = document.FindList("changed_nodes")) {
    for (const base::Value& value : *changed) {
      const base::DictValue* entry = value.GetIfDict();
      if (!entry) {
        continue;
      }
      auto changed_node = mojom::ChangedNode::New();
      const base::DictValue* node = entry->FindDict("node");
      changed_node->node =
          node ? Node(*node, delta->frame_id)
               : Node(base::DictValue(), delta->frame_id);
      changed_node->changed_fields = {mojom::SemanticField::kTextRuns};
      delta->changed_nodes.push_back(std::move(changed_node));
    }
  }
  if (const base::ListValue* removed = document.FindList("removed_node_ids")) {
    for (const base::Value& value : *removed) {
      if (const std::string* id = value.GetIfString()) {
        delta->removed_node_ids.push_back(*id);
      }
    }
  }
  delta->truncation = Truncation(document.FindDict("truncation"));
  delta->observed_at_monotonic_ms = Number(document, "observed_at_monotonic_ms");
  return mojom::PageDelta::Serialize(&delta);
}

std::vector<uint8_t> BuildPageInvalidation(const base::DictValue& document) {
  auto invalidation = mojom::PageInvalidation::New();
  invalidation->schema_version = Text(document, "schema_version");
  if (const std::string* subscription = document.FindString("subscription_id")) {
    invalidation->subscription_id = *subscription;
  }
  invalidation->tab_id = Text(document, "tab_id");
  invalidation->frame_id = Text(document, "frame_id");
  invalidation->page_epoch = Text(document, "page_epoch");
  invalidation->event_sequence = Number(document, "event_sequence");
  invalidation->reason = mojom::InvalidationReason::kCrossDocumentCommit;
  invalidation->retires_page_epoch =
      document.FindBool("retires_page_epoch").value_or(true);
  invalidation->invalidates_child_frames_only =
      document.FindBool("invalidates_child_frames_only").value_or(false);
  invalidation->resnapshot_required =
      document.FindBool("resnapshot_required").value_or(true);
  if (const std::string* replacement = document.FindString("new_page_epoch")) {
    invalidation->new_page_epoch = *replacement;
  }
  invalidation->observed_at_monotonic_ms =
      Number(document, "observed_at_monotonic_ms");
  return mojom::PageInvalidation::Serialize(&invalidation);
}

std::vector<uint8_t> BuildBackpressureNotice(const base::DictValue& document) {
  auto notice = mojom::BackpressureNotice::New();
  notice->schema_version = Text(document, "schema_version");
  notice->subscription_id = Text(document, "subscription_id");
  notice->tab_id = Text(document, "tab_id");
  notice->frame_id = Text(document, "frame_id");
  notice->page_epoch = Text(document, "page_epoch");
  notice->event_sequence = Number(document, "event_sequence");
  notice->action = mojom::BackpressureAction::kCoalesced;
  notice->dropped_delta_count =
      static_cast<uint32_t>(Number(document, "dropped_delta_count"));
  notice->dropped_categories = {mojom::DeltaCategory::kText,
                                mojom::DeltaCategory::kLayout};
  notice->resnapshot_required =
      document.FindBool("resnapshot_required").value_or(false);
  notice->observed_at_monotonic_ms =
      Number(document, "observed_at_monotonic_ms");
  return mojom::BackpressureNotice::Serialize(&notice);
}

}  // namespace

// static
std::vector<std::string> WireSeedBuilder::SupportedDefinitions() {
  return {"ProtocolInfo", "PageSnapshot", "PageDelta", "PageInvalidation",
          "BackpressureNotice"};
}

// static
bool WireSeedBuilder::IsBrowserFacingRendererPayload(
    const std::string& definition) {
  // The messages a renderer sends the browser. Everything else in the contract
  // is browser-authored — a proposal lives between the core service and the policy
  // engine, an authorized envelope stops in the browser process, and a renderer
  // command travels the other way — so none of them is an input on this
  // boundary and none needs a seed here.
  for (const std::string& supported : SupportedDefinitions()) {
    if (supported == definition) {
      return true;
    }
  }
  return false;
}

// static
std::optional<std::vector<uint8_t>> WireSeedBuilder::Build(
    const std::string& definition,
    const base::DictValue& document) {
  if (definition == "ProtocolInfo") {
    return BuildProtocolInfo(document);
  }
  if (definition == "PageSnapshot") {
    return BuildPageSnapshot(document);
  }
  if (definition == "PageDelta") {
    return BuildPageDelta(document);
  }
  if (definition == "PageInvalidation") {
    return BuildPageInvalidation(document);
  }
  if (definition == "BackpressureNotice") {
    return BuildBackpressureNotice(document);
  }
  return std::nullopt;
}

}  // namespace taffy::test
