// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SEMANTIC_GRAPH_H_
#define TAFFY_RENDERER_SEMANTIC_GRAPH_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/types/strong_alias.h"

// The renderer-local semantic graph: plain owned data with no Blink, no Mojo,
// and no Chromium object reference in it at all.
//
// Everything an adapter produces lands in these structs, and only
// PageIntelligenceEndpoint translates them to the wire. That separation earns
// its keep three ways:
//   * these types are testable on any host, which is where the identity and
//     redaction invariants are actually proved;
//   * no Blink object outlives the extraction that produced it, because no
//     Blink object is ever stored;
//   * the adapters do not have to be rewritten every time the wire schema
//     moves - and it has already moved once.
//
// Every enum below mirrors its counterpart in
// //taffy/contracts/bip/mojom/page_intelligence.mojom member for member, and
// the conversion functions in page_intelligence_endpoint.cc are switches with
// no default case. A member added on either side is a compile error rather than
// a silent fall-through, which for an action type or a sensitivity class is
// the difference between a build failure and a security bug.
//
// Contract: docs/architecture/browser-intelligence-protocol.md sections 5
// and 7.

namespace taffy {

// Identity is opaque and broker-assigned (protocol section 5). Frame ids and
// page
// epochs are strings the browser chose and this process only ever echoes;
// node ids are allocated here, inside one (FrameId, PageEpoch), and are
// meaningless outside it. Equality is the only defined operation - nothing
// infers order, structure, or a Chromium address from any of them.
using SemanticNodeId = base::StrongAlias<class SemanticNodeIdTag, std::string>;
using PageEpoch = base::StrongAlias<class PageEpochTag, std::string>;
using FrameId = base::StrongAlias<class FrameIdTag, std::string>;

// Monotonic within one active page epoch (protocol section 5.3).
using GraphRevision = base::StrongAlias<class GraphRevisionTag, uint64_t>;

// Mirrors mojom::SemanticRole. The vocabulary is [Open (OD-028)] until the
// golden-corpus review.
enum class SemanticRole {
  kDocument,
  kRegion,
  kHeading,
  kParagraph,
  kList,
  kListItem,
  kTable,
  kTableRow,
  kTableCell,
  kLink,
  kButton,
  kSearchField,
  kTextField,
  kCheckbox,
  kRadio,
  kSelect,
  kOption,
  kImage,
  kMedia,
  kPrice,
  kRating,
  kAvailability,
  kLabelValuePair,
  kCitation,
  kUnknownInteractive,
  kUnknownContent,
};

namespace renderer {

// Mirrors mojom::NodeState. Both a state and its negation exist so that "not
// asserted" stays distinguishable from "asserted false": an action policy
// that requires a state is not satisfied by silence, and an adapter that
// could not determine a state emits neither member.
// --- The mirror namespace -------------------------------------------------
//
// Everything between `namespace renderer {` and its close below is a name this
// file and //taffy/common/public BOTH define, differently, in namespace
// taffy. That is not a duplicate to be merged: renderer/DEPS forbids this half
// from including the public half at all, precisely so the two vocabularies
// cannot be passed across the process boundary by accident. Two definitions
// are the design.
//
// Two definitions of one *type name* in one namespace are not. C++ merges
// them by mangled name at link time, and the two disagree about layout:
// every enum here defaults to `int` while its public twin is fixed at
// `uint8_t`, and `Destination` carries a `std::string url` here and a
// `UrlMetadata url_metadata` there. `std::vector<taffy::NodeState>` therefore
// had two incompatible bodies with one symbol name, and the linker kept one:
// MEASURED on 2026-08-20 under AddressSanitizer as a heap-buffer-overflow,
// `WRITE of size 8` into a 2-byte allocation, inside
// `vector<taffy::NodeState>::__assign_with_size` reached from
// AccessibilityAdapter::Run - one body allocating with a 1-byte element and
// another copying with a 4-byte one. It took down every RedactionCanaryTest
// case and three AdapterTest cases through PartitionAlloc's trailing-cookie
// check, which fires at the next free rather than at the write.
//
// The nested namespace gives each of these a mangled name of its own, so
// nothing is shared and nothing is merged. The using-declaration after each
// one keeps every renderer call site spelled exactly as it was. A translation
// unit that manages to include both halves now fails to compile on an
// ambiguous name, which is the loud version of what used to be silent.
enum class NodeState {
  kVisible,
  kNotVisible,
  kOffscreen,
  kObscured,
  kEnabled,
  kDisabled,
  kEditable,
  kReadOnly,
  kRequired,
  kInvalid,
  kChecked,
  kUnchecked,
  kMixed,
  kSelected,
  kExpanded,
  kCollapsed,
  kFocused,
  kBusy,
};

}  // namespace renderer

using renderer::NodeState;

namespace renderer {

// Mirrors mojom::Sensitivity. Note what is absent: there is no "prohibited"
// class. A prohibited value is not a sensitivity level, it is a value that
// was never read - see field_redaction.h. The control is still described,
// with sensitivity kCredential and ValueKind::kSecretWithheld.
enum class Sensitivity {
  kNotSensitive,
  kPersonal,
  kAccount,
  kPayment,
  kIdentity,
  kHealth,
  kFinancial,
  kLegal,
  kPrivateCommunication,
  kAdministration,
  kCredential,
  kUnknownSensitive,
  kOneTimeCode,
  kChallengeResponse,
};

}  // namespace renderer

using renderer::Sensitivity;

namespace renderer {

// Mirrors mojom::ChallengeKind. A hint may improve a person-facing surface;
// it never authorizes an action and never suppresses handover.
enum class ChallengeKind {
  kNone,
  kImage,
  kInteractive,
  kOneTimeCode,
};

}  // namespace renderer

using renderer::ChallengeKind;

// Mirrors mojom::SourceKind (protocol section 7.5).
enum class SourceKind {
  kDom,
  kAccessibility,
  kFormControl,
  kJsonLd,
  kMicrodata,
  kBrowser,
  kAdapter,
};

// Mirrors mojom::Transformation. `kInferred` is a claim about the adapter,
// not about the page: it means no source stated this directly.
enum class Transformation {
  kNone,
  kNormalized,
  kInferred,
  kRedacted,
};

// The subset of mojom::ContentTrust a renderer is allowed to assert: who
// authored a unit of page content, as distinct from how much harm disclosing
// it would cause (protocol section 9.4). User-, Taffy-, and model-authored
// content can only be minted by trusted layers, so those three wire members
// deliberately have no representation in this process. A compromised
// renderer may still forge the Mojo number; the browser independently refuses
// it at the next boundary.
enum class RendererContentTrust {
  kFirstPartyDocument,
  kUserGeneratedContent,
  kThirdPartyEmbedded,
  kUnknownUntrusted,
};

// Mirrors mojom::ContentSignal: one observation about the shape or presentation
// of content, accumulated during the traversal this endpoint already performs
// (protocol section 9.4). A signal is evidence and never an authorization.
enum class ContentSignal {
  kHiddenByStyle,
  kZeroWidthCharacters,
  kBidiControlCharacters,
  kEncodedBlob,
  kImperativeInstructionShape,
  kLanguageMismatch,
  kCrossOriginFrameAuthored,
};

// The accumulator the walk actually uses. Signals are ORed in as each node is
// visited, so finding them costs a few operations per node rather than a second
// traversal. This mask never crosses the process boundary: the serializer
// expands it into the closed enumeration above, because a mask cannot fail
// closed - a reader silently ignores a bit it does not know, which is exactly
// what the contract forbids everywhere else.
using ContentSignalMask = uint32_t;

constexpr ContentSignalMask ContentSignalBit(ContentSignal signal) {
  return 1u << static_cast<uint32_t>(signal);
}

namespace renderer {

// Mirrors mojom::SemanticField: which field of a node a piece of evidence
// explains. Evidence is per field so that two sources disagreeing about one
// field is representable without discarding either (protocol section 7.5).
enum class SemanticField {
  kRole,
  kName,
  kDescription,
  kTextRuns,
  kStates,
  kValueDescriptor,
  kDestination,
  kBounds,
  kActions,
  kAttributes,
  kSensitivity,
  kEdges,
  kFrames,
  kContentTrust,
  kContentSignals,
  kChallengeKind,
};

}  // namespace renderer

using renderer::SemanticField;

// Mirrors mojom::RelationshipKind (protocol section 7.4).
enum class EdgeType {
  kContains,
  kLabels,
  kDescribes,
  kControls,
  kOwns,
  kRowHeaderFor,
  kColumnHeaderFor,
  kSameEntityAs,
  kSourceFor,
};

// Mirrors mojom::ActionType.
//
// It did not, until protocol 0.8. The four writing operations were reserved
// wire values then, so this enum omitted them and the wire-to-domain
// translation refused them by having nowhere to put them. That was a good
// refusal while there was nothing to refuse *to*; it is the wrong shape now
// that each one names a specific accessibility action this endpoint performs
// (action_accessibility_mapping.h, decision 0059), because an operation the
// domain cannot name is an operation no switch in this process is forced to
// handle.
//
// Naming them costs nothing that mattered. What kept a write from happening
// was never this enum: it is the milestone that owns the tool, the action
// class no milestone authorizes, the browser dispatcher's own refusal, and
// the fact that no adapter puts one of these in a node's actions[] - so a
// command naming one is refused for want of an available action before any
// of this is reached.
enum class ActionKind {
  kActivate,
  kFocus,
  kScrollIntoView,
  kSetText,
  kSelectOption,
  kToggle,
  kSubmitForm,
};

// Mirrors mojom::ValueKind.
enum class ValueKind {
  kText,
  kNumber,
  kDate,
  kSelection,
  kToggle,
  kPrice,
  kRating,
  kAvailability,
  // The control has a value and BIP will not carry it (protocol section 9.1).
  kSecretWithheld,
  kUnknown,
};

// Mirrors mojom::AttributeName. An allowlist, not a filter: an attribute with
// no member here cannot be emitted at all, which is what keeps arbitrary DOM
// attribute maps off the wire (protocol section 7.3).
enum class AttributeKey {
  kInputType,
  kAutocompleteToken,
  kHeadingLevel,
  kListPosition,
  kListSize,
  kTableRowIndex,
  kTableColumnIndex,
  kLanguage,
  kLinkRelation,
  kMediaAltSource,
  kCurrencyCode,
  kRatingScaleMax,
  kPlaceholderLabel,
};

// How an adapter reached a piece of content. Recorded on a node so that the
// snapshot can state, per node, that shadow content arrived through the
// composed tree rather than through a bypass (protocol section 8.2), and that
// a virtualized row is a fresh identity rather than a reused one
// (protocol section 8.3). Renderer-internal: the wire carries the same facts
// as warnings and evidence rather than as a node field.
enum class ProjectionPath {
  kLightTree,
  kComposedTreeThroughAccessibility,
  kVirtualizedRecycledSlot,
};

// Mirrors mojom::WarningCode. A stable code plus a bounded detail string -
// never a sentence, and never anything a page could influence the wording of
// (protocol section 11.7).
enum class WarningCode {
  kAdapterUnavailable,
  kAdapterFailed,
  kConflictingEvidence,
  kCrossOriginFrameOmitted,
  kClosedShadowRootNotProjected,
  kVirtualizedContentPartial,
  kCanvasWithoutSemantics,
  kSensitiveZoneSuppressed,
  kUrlMinimizedByPolicy,
  kDeadlineReached,
  kMemoryPressure,
  kInjectionSignalDetected,
};

namespace renderer {

// The adapters this endpoint has. Seven, one responsibility each, in the
// precedence order of protocol section 7.6.
//
// Every one of them maps onto a mojom::AdapterKind member. The two that had
// none - selection and layout - got one in the additive minor contract step
// recorded in taffy-core/contracts/bip/schema/bip.version.json, so
// wire::ToMojom is total for this enumeration and no adapter is described in a
// warning because the wire had no word for it.
//
// bip-local-vocabulary: this list is still not the wire's list, and it is not
//   meant to become it. It is what this process can run, so it omits the
//   document-viewer, media, site and vision adapters the protocol has names
//   for and this renderer does not have. Two members are also named for the
//   evidence they carry rather than for the wire's word for it, so a domain
//   type and a wire type are never confused at a call site. The mapping lives
//   in one place, wire_enum_conversions.cc, where every member of both
//   enumerations has a case: the four wire members this process cannot serve
//   resolve to nothing rather than to a near neighbour.
enum class AdapterKind {
  kDom,
  kAccessibility,
  kForms,
  // JSON-LD and microdata: what the page publishes about itself
  // (CAP-PI-004). Maps to mojom::AdapterKind::kMetadata, which is what
  // "structured metadata for entity attributes" names in the closed wire
  // enumeration.
  kStructuredData,
  // Document, origin and lifecycle facts the BROWSER stated. Maps to
  // mojom::AdapterKind::kBrowser, which is precedence item 1 of protocol
  // section 7.6 - browser-owned committed navigation and lifecycle state.
  kDocumentMetadata,
  // The user's current selection (CAP-PI-005). Maps to
  // mojom::AdapterKind::kSelection.
  kSelection,
  // Visibility, occlusion and bounds (CAP-PI-006). Maps to
  // mojom::AdapterKind::kLayout.
  kLayout,
};

}  // namespace renderer

using renderer::AdapterKind;

// Mirrors mojom::DocumentLifecycleState. The browser owns this: a renderer
// has opinions about its own lifecycle and the browser has the record
// (protocol section 5.5).
//
// There is deliberately no "unknown" member. Not having been told a lifecycle
// is the absence of a value, not a lifecycle, and giving it a member here
// would put a state on the wire that the contract does not define - which is
// exactly the coercion protocol section 6.2 forbids. Every holder of one of
// these that might not have been told yet uses std::optional and refuses while
// it is empty.
enum class DocumentLifecycle {
  kActive,
  kSpeculative,
  kPendingCommit,
  kPrerendering,
  kFrozen,
  kBackForwardCached,
  kCrashed,
  kDestroyed,
};

namespace renderer {

// Mirrors mojom::AdapterStatus (protocol sections 7.6 and 15). An adapter has
// three ways to be honest about an incomplete answer and no way to be
// dishonest about a complete one.
enum class AdapterStatus {
  kOk,
  kIncomplete,
  kConflicted,
  kUnsupported,
  kFailed,
};

}  // namespace renderer

using renderer::AdapterStatus;

struct FieldEvidence {
  FieldEvidence();
  FieldEvidence(SemanticField field,
                SourceKind source_kind,
                std::string source_locator,
                uint32_t extraction_rule_version,
                Transformation transformation);
  FieldEvidence(const FieldEvidence&);
  FieldEvidence(FieldEvidence&&);
  FieldEvidence& operator=(const FieldEvidence&);
  FieldEvidence& operator=(FieldEvidence&&);
  ~FieldEvidence();

  SemanticField field = SemanticField::kRole;
  SourceKind source_kind = SourceKind::kAdapter;

  // Bounded, internal, diagnostic. Spec section 7.5: not a reusable action
  // identity, and nothing may resolve a node from it.
  std::string source_locator;
  uint32_t extraction_rule_version = 0;
  Transformation transformation = Transformation::kNone;

  // Absent means "this adapter does not produce a confidence", which is not
  // the same as low confidence.
  std::optional<double> confidence;
};

struct TextRun {
  TextRun();
  TextRun(const TextRun&);
  TextRun(TextRun&&);
  TextRun& operator=(const TextRun&);
  TextRun& operator=(TextRun&&);
  ~TextRun();

  std::string text;
  SourceKind source_kind = SourceKind::kDom;
  Sensitivity sensitivity = Sensitivity::kUnknownSensitive;
  bool truncated = false;
  // Optional bounded citation coordinates. They describe where generated
  // media text came from and are never reusable action identities.
  std::optional<std::string> source_locator;
  std::optional<uint32_t> source_start;
  std::optional<uint32_t> source_end;
  // Fail closed even when an adapter forgets to label a run. The builder's
  // normal path replaces this with a browser-framed document label while the
  // run is produced; serialization never treats absence as first party.
  RendererContentTrust content_trust = RendererContentTrust::kUnknownUntrusted;
  // Internal mask only. The serializer expands it into the bounded closed
  // enumeration so an unknown signal cannot be ignored as an unknown bit.
  ContentSignalMask content_signals = 0;
};

// Spec section 7.3. This type cannot hold a secret because nothing ever puts
// one in it: ProhibitedValueFilter is consulted before a value is read from
// the page, not after, and a withheld value is represented by
// ValueKind::kSecretWithheld with no other member populated.
struct ValueDescriptor {
  ValueDescriptor();
  ValueDescriptor(const ValueDescriptor&);
  ValueDescriptor(ValueDescriptor&&);
  ValueDescriptor& operator=(const ValueDescriptor&);
  ValueDescriptor& operator=(ValueDescriptor&&);
  ~ValueDescriptor();

  ValueKind kind = ValueKind::kUnknown;
  // Whether the control has a value at all. False for a withheld secret:
  // emptiness is itself a fact about a secret (protocol section 9.1).
  bool present = false;
  bool redacted = false;
  std::optional<std::string> normalized_value;
  std::optional<std::string> unit;
  std::optional<std::string> currency_code;
};

struct Attribute {
  Attribute();
  Attribute(AttributeKey key, std::string value);
  Attribute(const Attribute&);
  Attribute(Attribute&&);
  Attribute& operator=(const Attribute&);
  Attribute& operator=(Attribute&&);
  ~Attribute();

  AttributeKey key = AttributeKey::kLanguage;
  std::string value;
};

// Viewport-relative and diagnostic only (protocol section 7.3): never identity,
// and never resolved from by the action path.
struct NodeBounds {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
};

namespace renderer {

// The renderer's view of where a node leads. The wire form carries a
// policy-controlled disclosure level; this one carries what was observed, and
// the endpoint decides how much of it may be stated.
struct Destination {
  Destination();
  Destination(const Destination&);
  Destination(Destination&&);
  Destination& operator=(const Destination&);
  Destination& operator=(Destination&&);
  ~Destination();

  // As resolved by Blink. Normalization for comparison is the browser's job;
  // the renderer must not be the thing that decides two URLs are the same.
  std::string url;
  bool is_cross_origin = false;
  bool opens_new_tab = false;
  bool is_download = false;
};

}  // namespace renderer

using renderer::Destination;

struct SemanticNode {
  SemanticNode();
  SemanticNode(const SemanticNode&);
  SemanticNode(SemanticNode&&);
  SemanticNode& operator=(const SemanticNode&);
  SemanticNode& operator=(SemanticNode&&);
  ~SemanticNode();

  SemanticNodeId node_id;
  FrameId frame_id;
  SemanticRole role = SemanticRole::kUnknownContent;

  std::optional<std::string> name;
  std::optional<std::string> description;
  std::vector<TextRun> text_runs;

  std::vector<NodeState> states;
  std::optional<ValueDescriptor> value_descriptor;
  std::optional<Destination> destination;
  std::optional<NodeBounds> bounds;

  std::vector<ActionKind> actions;
  Sensitivity sensitivity = Sensitivity::kUnknownSensitive;
  ChallengeKind challenge_kind = ChallengeKind::kNone;
  std::vector<SourceKind> sources;
  double confidence = 0.0;
  std::vector<Attribute> attributes;

  // Authorship and presentation evidence travel with the node rather than
  // being reconstructed by a consumer from its role, sensitivity, or source.
  // Unknown is the safe default and no renderer type can name the three
  // privileged authorship labels.
  RendererContentTrust content_trust = RendererContentTrust::kUnknownUntrusted;
  ContentSignalMask content_signals = 0;

  // Conflicting evidence is kept, never collapsed (protocol section 7.5).
  std::vector<FieldEvidence> evidence;

  // How this node was reached. Diagnostic and renderer-internal; it exists so
  // a test can assert that closed shadow content arrived through the
  // accessibility path and never through a bespoke traversal.
  ProjectionPath projection_path = ProjectionPath::kLightTree;
};

struct SemanticEdge {
  SemanticEdge();
  SemanticEdge(const SemanticEdge&);
  SemanticEdge(SemanticEdge&&);
  SemanticEdge& operator=(const SemanticEdge&);
  SemanticEdge& operator=(SemanticEdge&&);
  ~SemanticEdge();

  FrameId from_frame_id;
  SemanticNodeId from_node_id;
  FrameId to_frame_id;
  SemanticNodeId to_node_id;
  EdgeType relationship = EdgeType::kContains;
  bool inferred = false;
  std::optional<double> confidence;
};

struct ObservationWarning {
  ObservationWarning();
  ObservationWarning(WarningCode code, std::string detail_code);
  ObservationWarning(const ObservationWarning&);
  ObservationWarning(ObservationWarning&&);
  ObservationWarning& operator=(const ObservationWarning&);
  ObservationWarning& operator=(ObservationWarning&&);
  ~ObservationWarning();

  WarningCode code = WarningCode::kAdapterFailed;
  std::optional<SemanticNodeId> node_id;
  // A short stable token, chosen from a fixed local set.
  std::string detail_code;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_SEMANTIC_GRAPH_H_
