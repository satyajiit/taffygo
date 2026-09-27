// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/adapter.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/check.h"
#include "taffy/renderer/observation_limits.h"

namespace taffy {

namespace {

size_t SaturatingAdd(size_t left, size_t right) {
  const size_t ceiling = std::numeric_limits<size_t>::max();
  return right > ceiling - left ? ceiling : left + right;
}

size_t SaturatingMultiply(size_t left, size_t right) {
  const size_t ceiling = std::numeric_limits<size_t>::max();
  return left != 0 && right > ceiling / left ? ceiling : left * right;
}

void AddBytes(size_t bytes, size_t* total) {
  *total = SaturatingAdd(*total, bytes);
}

void AddString(const std::string& value, size_t* total) {
  AddBytes(value.size(), total);
}

uint32_t SignalCount(ContentSignalMask signals) {
  uint32_t count = 0;
  while (signals != 0) {
    count += signals & 1u;
    signals >>= 1u;
  }
  return count;
}

void AddOmittedText(size_t bytes, TruncationReport* report) {
  const uint32_t addition =
      static_cast<uint32_t>(std::min<size_t>(bytes, UINT32_MAX));
  report->omitted_text_bytes =
      addition > UINT32_MAX - report->omitted_text_bytes
          ? UINT32_MAX
          : report->omitted_text_bytes + addition;
}

// The framing terms, read from the one file that owns every provisional
// number this component enforces. Reached through a function rather than held
// in a member, because the four `Conservative*Bytes` helpers are static and
// their three call sites cannot all produce a limits object.
// `ProcessSafeCeiling()` is a process constant, and `NarrowedTo` deliberately
// leaves the framing alone, so a narrowed request and this agree.
const SnapshotBudget& Framing() {
  return ObservationLimits::ProcessSafeCeiling().snapshot();
}

}  // namespace

BudgetLedger::BudgetLedger(const SnapshotBudget& budget)
    : max_nodes_(budget.max_nodes()),
      max_text_bytes_(budget.max_text_bytes()),
      max_total_bytes_(budget.max_total_bytes()),
      max_depth_(budget.max_depth()),
      deadline_(budget.deadline()),
      started_at_(base::TimeTicks::Now()) {}

BudgetLedger::~BudgetLedger() = default;

void BudgetLedger::MarkTruncated(BudgetKind kind) {
  if (!report_.truncated) {
    report_.truncated = true;
    report_.first_budget_reached = kind;
  }
}

bool BudgetLedger::ChargeNode() {
  if (!CheckDeadline()) {
    return false;
  }
  if (nodes_ >= max_nodes_) {
    MarkTruncated(BudgetKind::kNodes);
    return false;
  }
  ++nodes_;
  return true;
}

bool BudgetLedger::ChargeText(size_t bytes) {
  if (bytes > static_cast<size_t>(max_text_bytes_) - text_bytes_) {
    MarkTruncated(BudgetKind::kTextBytes);
    return false;
  }
  if (!ChargeBytes(bytes)) {
    return false;
  }
  text_bytes_ += bytes;
  return true;
}

bool BudgetLedger::ChargeBytes(size_t bytes) {
  if (bytes > static_cast<size_t>(max_total_bytes_) - total_bytes_) {
    MarkTruncated(BudgetKind::kTotalBytes);
    return false;
  }
  total_bytes_ += bytes;
  return true;
}

bool BudgetLedger::ChargeNodePayload(const SemanticNode& node) {
  return ChargeBytes(ConservativeNodeBytes(node));
}

bool BudgetLedger::ChargeEdge(const SemanticEdge& edge) {
  return ChargeBytes(ConservativeEdgeBytes(edge));
}

bool BudgetLedger::ChargeAnnotation(const NodeAnnotation& annotation) {
  return ChargeBytes(ConservativeAnnotationBytes(annotation));
}

bool BudgetLedger::ChargeWarning(const ObservationWarning& warning) {
  return ChargeBytes(ConservativeWarningBytes(warning));
}

bool BudgetLedger::WithinDepth(uint32_t depth) {
  if (depth > max_depth_) {
    MarkTruncated(BudgetKind::kDepth);
    return false;
  }
  return true;
}

bool BudgetLedger::WithinDeadline() const {
  // Const, so it does not mark truncation itself; the callers that can stop
  // do that. A deadline check that mutates from a const method is the kind of
  // surprise that makes a traversal hard to reason about.
  return base::TimeTicks::Now() - started_at_ < deadline_;
}

bool BudgetLedger::CheckDeadline() {
  if (WithinDeadline()) {
    return true;
  }
  MarkTruncated(BudgetKind::kDeadline);
  return false;
}

bool BudgetLedger::CheckBytesAvailable(size_t bytes) {
  if (bytes <= static_cast<size_t>(max_total_bytes_) - total_bytes_) {
    return true;
  }
  MarkTruncated(BudgetKind::kTotalBytes);
  return false;
}

size_t BudgetLedger::RemainingTextBytes(size_t own_ceiling) const {
  const size_t shared_remaining =
      max_text_bytes_ > text_bytes_
          ? static_cast<size_t>(max_text_bytes_) - text_bytes_
          : 0u;
  const size_t total_remaining =
      max_total_bytes_ > total_bytes_
          ? static_cast<size_t>(max_total_bytes_) - total_bytes_
          : 0u;
  return std::min({shared_remaining, total_remaining, own_ceiling});
}

size_t BudgetLedger::RemainingNodes() const {
  return max_nodes_ > nodes_ ? static_cast<size_t>(max_nodes_ - nodes_) : 0u;
}

void BudgetLedger::NoteOmittedTextBytes(size_t bytes_at_least,
                                        bool could_change_answer) {
  const size_t shared_remaining =
      max_text_bytes_ > text_bytes_
          ? static_cast<size_t>(max_text_bytes_) - text_bytes_
          : 0u;
  const size_t total_remaining =
      max_total_bytes_ > total_bytes_
          ? static_cast<size_t>(max_total_bytes_) - total_bytes_
          : 0u;
  MarkTruncated(total_remaining <= shared_remaining ? BudgetKind::kTotalBytes
                                                    : BudgetKind::kTextBytes);
  AddOmittedText(bytes_at_least, &report_);
  report_.omitted_data_could_change_answer |= could_change_answer;
}

// static
size_t BudgetLedger::ConservativeNodeBytes(const SemanticNode& node) {
  // The node's framing, not `sizeof(SemanticNode)`.
  //
  // The struct is this process's resident cost: on a 64-bit build its empty
  // optionals and empty vectors come to roughly 540 bytes that are nothing on
  // either hop out. Charging it made `max_total_bytes` mean one quantity here
  // and another in the browser, which spends the same number encoding the
  // payload and then reports `total_bytes = graph_payload.size()`. A phone
  // read 397 of a page's 658 nodes and stopped on that budget while the graph
  // that travelled was 24 KB of the 256 KB it was allowed. Decision 0166.
  size_t bytes = Framing().node_framing_bytes();
  AddString(node.node_id.value(), &bytes);
  AddString(node.frame_id.value(), &bytes);
  if (node.name.has_value()) {
    AddString(*node.name, &bytes);
  }
  if (node.description.has_value()) {
    AddString(*node.description, &bytes);
  }

  AddBytes(SaturatingMultiply(node.text_runs.size(),
                              Framing().text_run_framing_bytes()),
           &bytes);
  for (const TextRun& run : node.text_runs) {
    AddString(run.text, &bytes);
    AddBytes(SaturatingMultiply(SignalCount(run.content_signals),
                                Framing().list_element_bytes()),
             &bytes);
  }
  AddBytes(SaturatingMultiply(node.states.size(), Framing().list_element_bytes()),
           &bytes);
  if (node.value_descriptor.has_value()) {
    const ValueDescriptor& value = *node.value_descriptor;
    if (value.normalized_value.has_value()) {
      AddString(*value.normalized_value, &bytes);
    }
    if (value.unit.has_value()) {
      AddString(*value.unit, &bytes);
    }
    if (value.currency_code.has_value()) {
      AddString(*value.currency_code, &bytes);
    }
  }
  if (node.destination.has_value()) {
    AddString(node.destination->url, &bytes);
  }
  AddBytes(SaturatingMultiply(node.actions.size(), Framing().list_element_bytes()),
           &bytes);
  AddBytes(SaturatingMultiply(node.sources.size(), Framing().list_element_bytes()),
           &bytes);
  // Attributes, evidence and sources are not written into the BIP graph
  // payload, but they do cross the renderer-to-browser pipe as mojo struct
  // fields, so their strings stay charged and only the fixed element term
  // changes.
  AddBytes(SaturatingMultiply(node.attributes.size(),
                              Framing().list_element_bytes()),
           &bytes);
  for (const Attribute& attribute : node.attributes) {
    AddString(attribute.value, &bytes);
  }
  AddBytes(SaturatingMultiply(node.evidence.size(),
                              Framing().list_element_bytes()),
           &bytes);
  for (const FieldEvidence& evidence : node.evidence) {
    // The vector-element object was counted immediately above.
    AddString(evidence.source_locator, &bytes);
  }
  AddBytes(SaturatingMultiply(SignalCount(node.content_signals),
                              Framing().list_element_bytes()),
           &bytes);
  return bytes;
}

// static
size_t BudgetLedger::ConservativeEdgeBytes(const SemanticEdge& edge) {
  size_t bytes = Framing().edge_framing_bytes();
  AddString(edge.from_frame_id.value(), &bytes);
  AddString(edge.from_node_id.value(), &bytes);
  AddString(edge.to_frame_id.value(), &bytes);
  AddString(edge.to_node_id.value(), &bytes);
  return bytes;
}

// static
size_t BudgetLedger::ConservativeAnnotationBytes(
    const NodeAnnotation& annotation) {
  size_t bytes = Framing().node_framing_bytes();
  AddString(annotation.node_id.value(), &bytes);
  AddBytes(SaturatingMultiply(annotation.states.size(),
                              Framing().list_element_bytes()),
           &bytes);
  AddBytes(SaturatingMultiply(annotation.evidence.size(),
                              Framing().list_element_bytes()),
           &bytes);
  for (const FieldEvidence& evidence : annotation.evidence) {
    AddString(evidence.source_locator, &bytes);
  }
  return bytes;
}

// static
size_t BudgetLedger::ConservativeWarningBytes(
    const ObservationWarning& warning) {
  size_t bytes = Framing().node_framing_bytes();
  if (warning.node_id.has_value()) {
    AddString(warning.node_id->value(), &bytes);
  }
  AddString(warning.detail_code, &bytes);
  return bytes;
}

void BudgetLedger::NoteOmittedNode(bool could_change_answer) {
  NoteOmittedNodes(1, could_change_answer);
}

void BudgetLedger::NoteOmittedNodes(size_t count, bool could_change_answer) {
  const uint32_t addition =
      static_cast<uint32_t>(std::min<size_t>(count, UINT32_MAX));
  report_.omitted_node_count =
      addition > UINT32_MAX - report_.omitted_node_count
          ? UINT32_MAX
          : report_.omitted_node_count + addition;
  report_.omitted_data_could_change_answer |= could_change_answer;
}

std::string BudgetLedger::BoundText(std::string_view text) {
  return BoundTextTo(text, max_text_bytes_);
}

std::string BudgetLedger::BoundTextTo(std::string_view text,
                                      size_t own_ceiling) {
  if (text.empty()) {
    return std::string();
  }
  const size_t shared_remaining =
      max_text_bytes_ > text_bytes_
          ? static_cast<size_t>(max_text_bytes_) - text_bytes_
          : 0u;
  const size_t total_remaining =
      max_total_bytes_ > total_bytes_
          ? static_cast<size_t>(max_total_bytes_) - total_bytes_
          : 0u;
  const size_t remaining = RemainingTextBytes(own_ceiling);
  if (remaining == 0) {
    MarkTruncated(total_remaining <= shared_remaining ? BudgetKind::kTotalBytes
                                                      : BudgetKind::kTextBytes);
    AddOmittedText(text.size(), &report_);
    return std::string();
  }
  if (text.size() > remaining) {
    MarkTruncated(total_remaining <= shared_remaining &&
                          total_remaining <= own_ceiling
                      ? BudgetKind::kTotalBytes
                      : BudgetKind::kTextBytes);
    AddOmittedText(text.size() - remaining, &report_);
    // Truncating a UTF-8 string at a byte offset can split a code point.
    // Walk back to a boundary rather than emitting an invalid sequence that
    // some downstream serializer would then have to guess about.
    size_t cut = remaining;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    text = text.substr(0, cut);
  }
  text_bytes_ += text.size();
  total_bytes_ += text.size();
  return std::string(text);
}

BrowserSuppliedFacts::BrowserSuppliedFacts() = default;
BrowserSuppliedFacts::BrowserSuppliedFacts(const BrowserSuppliedFacts&) =
    default;
BrowserSuppliedFacts& BrowserSuppliedFacts::operator=(
    const BrowserSuppliedFacts&) = default;
BrowserSuppliedFacts::~BrowserSuppliedFacts() = default;

NodeAnnotation::NodeAnnotation() = default;
NodeAnnotation::NodeAnnotation(const NodeAnnotation&) = default;
NodeAnnotation::NodeAnnotation(NodeAnnotation&&) = default;
NodeAnnotation& NodeAnnotation::operator=(const NodeAnnotation&) = default;
NodeAnnotation& NodeAnnotation::operator=(NodeAnnotation&&) = default;
NodeAnnotation::~NodeAnnotation() = default;

ExtractedGraph::ExtractedGraph() = default;
ExtractedGraph::ExtractedGraph(ExtractedGraph&&) = default;
ExtractedGraph& ExtractedGraph::operator=(ExtractedGraph&&) = default;
ExtractedGraph::~ExtractedGraph() = default;

ExtractionContext::ExtractionContext(blink::WebLocalFrame* frame,
                                     SemanticGraphStore* store,
                                     BudgetLedger* ledger,
                                     const ObservationLimits& limits,
                                     const ExtractedGraph* accumulated,
                                     const BrowserSuppliedFacts& browser_facts,
                                     ExtractionScope scope)
    : frame(frame),
      store(store),
      ledger(ledger),
      limits(limits),
      accumulated(accumulated),
      browser_facts(browser_facts),
      scope(scope),
      prohibited_value_filter(limits) {
  CHECK(frame);
  CHECK(store);
  CHECK(ledger);
}

ExtractionContext::~ExtractionContext() = default;

AdapterResult::AdapterResult() = default;
AdapterResult::AdapterResult(AdapterResult&&) = default;
AdapterResult& AdapterResult::operator=(AdapterResult&&) = default;
AdapterResult::~AdapterResult() = default;

Adapter::Adapter() = default;
Adapter::~Adapter() = default;

bool Adapter::annotates_existing_nodes() const {
  return false;
}

FieldEvidence Adapter::MakeEvidence(SemanticField field,
                                    SourceKind source_kind,
                                    std::string source_locator,
                                    Transformation transformation) const {
  return FieldEvidence(field, source_kind, std::move(source_locator),
                       extraction_rule_version(), transformation);
}

}  // namespace taffy
