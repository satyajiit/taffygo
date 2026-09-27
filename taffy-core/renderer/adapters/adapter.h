// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_ADAPTER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/time/time.h"
#include "taffy/renderer/field_redaction.h"
#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/semantic_graph.h"
#include "taffy/renderer/semantic_graph_store.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

// The contract every adapter implements, and the budget it works inside.
//
// Three properties are load-bearing:
//
//   * An adapter can always say less. AdapterStatus has three ways to be
//     honest about an incomplete answer (kUnsupported, kIncomplete,
//     kConflicted) and no way to be dishonest about a complete one. Protocol
//     section 7.6: an adapter must not fabricate a normalized value to
//     satisfy a requested schema.
//   * An adapter failing is isolated. Run() returns a status per adapter, and
//     one adapter's failure never discards another adapter's evidence
//     (protocol section 15).
//   * An adapter cannot invent a bound. It is handed an ObservationLimits it
//     did not build and cannot build - the grouped limit types have private
//     constructors - so every ceiling in this component traces back to
//     observation_limits.json.

// Which part of a document one request covers. Mirrors
// mojom::ObservationScope member for member; the conversion in
// wire_conversions.cc is a switch with no default case.
enum class ExtractionScope {
  kViewport,
  kInteractive,
  kSelection,
  kSection,
  kDocument,
};

namespace renderer {

// Nested for the same reason as the types in renderer/semantic_graph.h, and
// the reasoning there is the authority: //taffy/common/public defines a
// different `taffy::BudgetKind` (kMaxNodes, kMaxFrames, kMaxMessageBytes ...,
// fixed at uint8_t), renderer/DEPS forbids this half from including that one,
// and two different definitions of one type name in one namespace are merged
// by the linker into whichever body it happens to keep.
//
// Which budget stopped the extraction, so a truncated result can say what it
// ran out of instead of just being short (protocol section 15).
//
// bip-local-vocabulary: the wire names limits (kMaxNodes) and this names the
//   limit that was hit (kNodes), which is why kNone exists here and cannot
//   exist there — "no budget stopped anything" is not a budget. The two are
//   translated in wire_enum_conversions.cc rather than shared.
enum class BudgetKind {
  kNone,
  kNodes,
  kTextBytes,
  kTotalBytes,
  kDepth,
  kDeadline,
};

}  // namespace renderer

using renderer::BudgetKind;

struct NodeAnnotation;

struct TruncationReport {
  bool truncated = false;
  BudgetKind first_budget_reached = BudgetKind::kNone;
  uint32_t omitted_node_count = 0;
  uint32_t omitted_text_bytes = 0;
  // Set when the adapter knows the omitted region was relevant to the
  // request - for example, the requested scope was "document" and the
  // extraction stopped inside the main content. A caller must not treat a
  // truncated result as an answer when this is set.
  bool omitted_data_could_change_answer = false;
};

// Accounting shared by every adapter in one extraction. Passing one of these
// around instead of letting each adapter keep its own counters is what makes
// the total bound a real bound rather than a per-adapter bound multiplied by
// the number of adapters.
class BudgetLedger {
 public:
  explicit BudgetLedger(const SnapshotBudget& budget);
  BudgetLedger(const BudgetLedger&) = delete;
  BudgetLedger& operator=(const BudgetLedger&) = delete;
  ~BudgetLedger();

  // Each returns false when the charge would exceed the budget, and records
  // which budget was hit first. A false answer means "stop", never "trim and
  // continue silently".
  bool ChargeNode();
  bool ChargeText(size_t bytes);
  bool ChargeBytes(size_t bytes);
  bool ChargeNodePayload(const SemanticNode& node);
  bool ChargeEdge(const SemanticEdge& edge);
  bool ChargeAnnotation(const NodeAnnotation& annotation);
  bool ChargeWarning(const ObservationWarning& warning);
  bool WithinDepth(uint32_t depth);
  bool WithinDeadline() const;
  // Unlike WithinDeadline(), records the deadline in the truncation report.
  // Work loops use this form at every point where they can stop.
  bool CheckDeadline();

  // Checks an adapter's not-yet-merged payload against the bytes still
  // available without spending those bytes. This is for construction-time
  // caps: the builder performs the actual charge when it accepts the result.
  bool CheckBytesAvailable(size_t bytes);

  // Bytes a caller may still convert into text, after the shared text and
  // total-payload budgets and its own ceiling are all applied. Blink-facing
  // adapters use this before converting a WebString so an attacker-sized
  // UTF-16 value cannot become an attacker-sized temporary UTF-8 allocation.
  size_t RemainingTextBytes(size_t own_ceiling) const;

  // Node slots not yet claimed. Traversals subtract their already queued
  // entries from this before pushing children, keeping a single hostile fan-
  // out from allocating an unbounded work stack before ChargeNode can run.
  size_t RemainingNodes() const;

  // Records text deliberately left unconverted. `bytes_at_least` is a lower
  // bound when the source is still UTF-16; learning the exact UTF-8 size
  // would require the unbounded conversion this method exists to avoid.
  void NoteOmittedTextBytes(size_t bytes_at_least, bool could_change_answer);

  // Conservative serialized-payload accounting, and serialized only.
  //
  // The fixed term is the framing the encoder writes — `snapshot_node_framing_bytes`
  // and its three neighbours in observation_limits.json — never `sizeof` of
  // the struct this process holds. The two are not the same quantity, and
  // charging the resident one spent a budget the browser measures on the
  // encoded payload: a page whose graph was 24 KB stopped at 397 of 658 nodes
  // against a 256 KB budget (decision 0166).
  //
  // Every dynamic field is included, and text already charged while it was
  // read is deliberately counted again here. With the fixed resident term
  // gone, that double count is what keeps the total an upper bound rather
  // than an optimistic estimate, and it keeps one definition shared by
  // construction-time caps and the merge.
  static size_t ConservativeNodeBytes(const SemanticNode& node);
  static size_t ConservativeEdgeBytes(const SemanticEdge& edge);
  static size_t ConservativeAnnotationBytes(const NodeAnnotation& annotation);
  static size_t ConservativeWarningBytes(const ObservationWarning& warning);

  // True when any budget has been reached. Adapters check this in their
  // traversal loop.
  bool exhausted() const { return report_.truncated; }
  const TruncationReport& report() const { return report_; }
  void NoteOmittedNode(bool could_change_answer);
  void NoteOmittedNodes(size_t count, bool could_change_answer);

  // Bounds a single string to the remaining text budget, charging what it
  // keeps. Returns the bounded copy.
  std::string BoundText(std::string_view text);

  // Same, with an explicit per-string ceiling on top of the shared budget.
  // The selection adapter needs one: a user can select a whole document, and
  // one adapter must not be able to spend the entire text budget.
  std::string BoundTextTo(std::string_view text, size_t own_ceiling);

 private:
  void MarkTruncated(BudgetKind kind);

  const uint32_t max_nodes_;
  const uint32_t max_text_bytes_;
  const uint32_t max_total_bytes_;
  const uint32_t max_depth_;
  const base::TimeDelta deadline_;
  const base::TimeTicks started_at_;
  uint32_t nodes_ = 0;
  size_t text_bytes_ = 0;
  size_t total_bytes_ = 0;
  TruncationReport report_;
};

// Facts about this document that the BROWSER stated, not that the renderer
// observed.
//
// Protocol section 7.2 and section 7.6 both put document identity in the
// browser's hands: "renderer-reported URLs do not override the browser
// broker's committed navigation record", and browser-owned committed
// navigation and lifecycle state is the first source of precedence. The
// document metadata adapter emits these values and never the renderer's own,
// so this struct is how they reach it - by being handed in from the request
// the broker authored, rather than read out of Blink.
struct BrowserSuppliedFacts {
  BrowserSuppliedFacts();
  BrowserSuppliedFacts(const BrowserSuppliedFacts&);
  BrowserSuppliedFacts& operator=(const BrowserSuppliedFacts&);
  ~BrowserSuppliedFacts();

  std::string tab_id;
  std::string root_frame_id;
  std::string page_epoch;
  // The task purpose and sensitivity policy identifier the broker attached.
  // Neither is interpreted here; both are recorded as evidence so an audit
  // can say which policy an observation was made under.
  std::string sensitivity_policy_id;
  std::string task_purpose;
  // The lifecycle state the browser told this endpoint about. The renderer
  // has opinions about its own lifecycle; the browser has the record. Empty
  // until the browser states one, which is not the same as any lifecycle it
  // could state: an adapter that reads this while it is empty learns that
  // nothing is known, and the policy floor is already raised for that case.
  std::optional<DocumentLifecycle> lifecycle;
  // The strictest classification the browser already decided applies to this
  // document. A floor, never a ceiling.
  Sensitivity policy_floor = Sensitivity::kNotSensitive;
  // True when the broker told us this frame is not same-origin with the root
  // frame of the observation.
  bool cross_origin_frame = false;

  // The origins the broker said this observation may cover, serialized.
  // Empty means the root frame's own origin only, never "no restriction"
  // (mojom SnapshotRequest.allowed_origins). The renderer cannot widen this
  // and does not try to; the document metadata adapter compares its own view
  // of the document origin against it and reports a DISAGREEMENT, which is a
  // signal the broker wants and a renderer claim it would otherwise have no
  // way to make safely.
  std::vector<std::string> allowed_origin_serializations;
};

// A change to a node another adapter already produced.
//
// Layout and selection do not describe nodes of their own: visibility,
// occlusion, bounds, and "the user has this selected" are facts ABOUT the
// nodes the DOM, accessibility, and form adapters produced. Emitting them as
// annotations rather than letting one adapter reach into another's output
// keeps the merge in one reviewed place and keeps every adapter's Run() a
// pure function of the document plus its budget.
struct NodeAnnotation {
  NodeAnnotation();
  NodeAnnotation(const NodeAnnotation&);
  NodeAnnotation(NodeAnnotation&&);
  NodeAnnotation& operator=(const NodeAnnotation&);
  NodeAnnotation& operator=(NodeAnnotation&&);
  ~NodeAnnotation();

  SemanticNodeId node_id;
  // Appended, never replacing what the node already asserted. Two adapters
  // that disagree about a state both get to say so, and the merge keeps the
  // stricter reading.
  std::vector<NodeState> states;
  std::optional<NodeBounds> bounds;
  // Set when this annotation carries a positive occlusion determination -
  // that is, the adapter actually probed rather than declining to. An action
  // precondition that forbids occlusion is refused outright when no
  // annotation determined it, because "we did not look" is not "not
  // obscured" (protocol section 11.4, `[Open (OD-054)]`).
  bool occlusion_determined = false;
  std::vector<FieldEvidence> evidence;
};

// Everything produced so far in this extraction, in adapter precedence order.
// Adapters that annotate read it; adapters that produce nodes do not need it.
struct ExtractedGraph {
  ExtractedGraph();
  ExtractedGraph(const ExtractedGraph&) = delete;
  ExtractedGraph(ExtractedGraph&&);
  ExtractedGraph& operator=(const ExtractedGraph&) = delete;
  ExtractedGraph& operator=(ExtractedGraph&&);
  ~ExtractedGraph();

  std::vector<SemanticNode> nodes;
  std::vector<SemanticEdge> edges;
  std::vector<ObservationWarning> warnings;
};

// Everything an adapter is allowed to touch. Note what is absent: no browser
// interface, no capability, no policy object, no origin allowlist that the
// renderer could widen. The renderer is trusted for nothing, so it is handed
// nothing it could misuse.
struct ExtractionContext {
  ExtractionContext(blink::WebLocalFrame* frame,
                    SemanticGraphStore* store,
                    BudgetLedger* ledger,
                    const ObservationLimits& limits,
                    const ExtractedGraph* accumulated,
                    const BrowserSuppliedFacts& browser_facts,
                    ExtractionScope scope);
  ExtractionContext(blink::WebLocalFrame* frame,
                    SemanticGraphStore* store,
                    BudgetLedger* ledger,
                    const ObservationLimits& limits,
                    const ExtractedGraph* accumulated,
                    BrowserSuppliedFacts&& browser_facts,
                    ExtractionScope scope) = delete;
  ExtractionContext(const ExtractionContext&) = delete;
  ExtractionContext& operator=(const ExtractionContext&) = delete;
  ~ExtractionContext();

  const raw_ptr<blink::WebLocalFrame> frame;
  const raw_ptr<SemanticGraphStore> store;
  const raw_ptr<BudgetLedger> ledger;
  const raw_ref<const ObservationLimits> limits;
  // Read-only view of what earlier adapters produced. Null for the first
  // adapter in the order; annotating adapters check.
  const raw_ptr<const ExtractedGraph> accumulated;

  // One snapshot owns these facts for longer than every adapter run. Borrowing
  // them keeps each adapter context from deep-copying the browser's identity
  // strings and allowed-origin vector.
  const raw_ref<const BrowserSuppliedFacts> browser_facts;
  const ExtractionScope scope;

  // Shared redaction layer. Held by value but constructed from the same
  // limits so every adapter provably uses the same rules and rule version.
  const ProhibitedValueFilter prohibited_value_filter;
  const SensitivityClassifier sensitivity_classifier;

  // Convenience accessors keep adapter call sites independent of whether the
  // per-snapshot facts are owned or borrowed by this context.
  bool cross_origin_frame() const { return browser_facts->cross_origin_frame; }
  Sensitivity policy_floor() const { return browser_facts->policy_floor; }
};

struct AdapterResult {
  AdapterResult();
  AdapterResult(const AdapterResult&) = delete;
  AdapterResult& operator=(const AdapterResult&) = delete;
  AdapterResult(AdapterResult&&);
  AdapterResult& operator=(AdapterResult&&);
  ~AdapterResult();

  AdapterStatus status = AdapterStatus::kUnsupported;
  std::vector<SemanticNode> nodes;
  std::vector<SemanticEdge> edges;
  std::vector<NodeAnnotation> annotations;
  std::vector<ObservationWarning> warnings;
  TruncationReport truncation;
};

class Adapter {
 public:
  Adapter(const Adapter&) = delete;
  Adapter& operator=(const Adapter&) = delete;
  virtual ~Adapter();

  // Which adapter this is. Reported in the per-document capability response
  // and in every AdapterReport (protocol section 7.2, CAP-PI-008).
  virtual AdapterKind kind() const = 0;

  // Stable identifier reported in ProtocolInfo and in every FieldEvidence
  // this adapter produces.
  virtual std::string_view name() const = 0;

  // Bumped when this adapter's extraction rules change meaning.
  virtual uint32_t extraction_rule_version() const = 0;

  // True when this adapter reads nodes earlier adapters produced. The
  // registry runs producers first and annotators afterwards, and an annotator
  // that runs with nothing accumulated returns kUnsupported rather than
  // silently producing nothing.
  virtual bool annotates_existing_nodes() const;

  // Runs against the frame in `context`. Must not mutate the document, must
  // not run script, and must not block. An adapter that cannot honour those
  // three returns kUnsupported instead.
  virtual AdapterResult Run(ExtractionContext& context) = 0;

 protected:
  Adapter();

  // Helper so every adapter stamps provenance the same way. Evidence is per
  // field (protocol section 7.5): two sources disagreeing about one field of
  // one node has to be representable without discarding either.
  FieldEvidence MakeEvidence(SemanticField field,
                             SourceKind source_kind,
                             std::string source_locator,
                             Transformation transformation) const;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_ADAPTER_H_
