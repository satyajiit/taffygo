// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/document_metadata_adapter.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/renderer/content_metadata.h"
#include "third_party/blink/public/platform/web_security_origin.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"

// VERIFY AT SP-04:
//   * blink::WebDocument::GetSecurityOrigin(), and what
//     blink::WebSecurityOrigin::ToString() returns for an opaque origin.
//     Every opaque origin serializes to the same token, so an opaque document
//     must never be reported as agreeing with an allowlist entry by string
//     equality: the comparison below refuses instead, which is the only safe
//     reading. The browser's nonce comparison is the real one.
//   * blink::WebDocument::DocumentElement() and GetAttribute() spellings, for
//     the `lang` attribute.
//   * Whether the broker populates SnapshotRequest.allowed_origins on every
//     request or leaves it empty for the common same-origin case. The empty
//     case is documented as "the root frame's own origin only", and this
//     adapter treats it that way rather than as "no restriction".

namespace taffy {

namespace {

constexpr uint32_t kDocumentMetadataRuleVersion = 1;
constexpr char kAdapterName[] = "document-metadata";

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

// Whether this process's own view of the document origin sits inside the set
// the broker said the observation covers.
//
// Three answers, and the third is why this returns an enum rather than a
// bool: "the broker stated no set" is not agreement, and it is not
// disagreement either.
enum class OriginAgreement {
  kAgrees,
  kDisagrees,
  kBrokerStatedNoSet,
};

OriginAgreement CompareOrigin(const blink::WebDocument& document,
                              const std::vector<std::string>& allowed) {
  if (allowed.empty()) {
    return OriginAgreement::kBrokerStatedNoSet;
  }
  const blink::WebSecurityOrigin origin = document.GetSecurityOrigin();
  if (origin.IsNull() || origin.IsOpaque()) {
    // Every opaque origin serializes to the same token, so a string
    // comparison would report agreement between two unrelated opaque
    // documents. Refusing is the only honest answer available in this
    // process; the broker compares nonces and that comparison means
    // something.
    return OriginAgreement::kDisagrees;
  }
  const std::string serialization = origin.ToString().Utf8();
  return std::ranges::find(allowed, serialization) != allowed.end()
             ? OriginAgreement::kAgrees
             : OriginAgreement::kDisagrees;
}

}  // namespace

DocumentMetadataAdapter::DocumentMetadataAdapter() = default;
DocumentMetadataAdapter::~DocumentMetadataAdapter() = default;

AdapterKind DocumentMetadataAdapter::kind() const {
  return AdapterKind::kDocumentMetadata;
}

std::string_view DocumentMetadataAdapter::name() const {
  return kAdapterName;
}

uint32_t DocumentMetadataAdapter::extraction_rule_version() const {
  return kDocumentMetadataRuleVersion;
}

AdapterResult DocumentMetadataAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }

  const BrowserSuppliedFacts& facts = *context.browser_facts;
  if (facts.page_epoch.empty()) {
    // Without a browser-assigned epoch there is no identity to report, and
    // inventing one is the single thing a renderer must never do with an
    // identifier (protocol section 5.2).
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-browser-assigned-epoch");
    return result;
  }

  if (!context.ledger->ChargeNode()) {
    result.status = AdapterStatus::kIncomplete;
    result.truncation = context.ledger->report();
    return result;
  }

  // Ordinal zero of the derived identity space, deterministically, so the
  // document node is the first identity allocated in every extraction and is
  // never an action target: RendererActionExecutor refuses a derived identity
  // outright.
  const SemanticNodeId id = context.store->AllocateOrLookup(
      context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived, 0));

  SemanticNode node;
  node.node_id = id;
  node.frame_id = context.store->frame_id();
  node.role = SemanticRole::kDocument;
  node.sensitivity =
      context.cross_origin_frame()
          ? Sensitivity::kUnknownSensitive
          : SensitivityClassifier::Stricter(Sensitivity::kNotSensitive,
                                            context.policy_floor());
  node.confidence = 1.0;
  content_metadata::ApplyNodeContext(
      &node, content_metadata::DocumentTrust(context.cross_origin_frame()),
      content_metadata::ContextSignals(
          /*hidden_by_style=*/false, /*language_mismatch=*/false,
          context.cross_origin_frame()));
  node.sources.push_back(SourceKind::kBrowser);

  // Identity evidence. The locators name the broker-supplied values rather
  // than repeating them into a field a consumer might read as renderer-
  // authored: the snapshot envelope already carries tab, frame, and epoch
  // from the request, and restating them here would create a second copy for
  // the broker to have to reconcile.
  node.evidence.push_back(
      MakeEvidence(SemanticField::kFrames, SourceKind::kBrowser,
                   "browser/page-epoch", Transformation::kNone));
  if (!facts.sensitivity_policy_id.empty()) {
    // Recorded, never interpreted. An audit needs to be able to say which
    // policy an observation was made under; this process must not act on it.
    node.evidence.push_back(MakeEvidence(
        SemanticField::kSensitivity, SourceKind::kBrowser,
        "browser/sensitivity-policy/" + facts.sensitivity_policy_id,
        Transformation::kNone));
  }
  if (!facts.task_purpose.empty()) {
    node.evidence.push_back(
        MakeEvidence(SemanticField::kName, SourceKind::kBrowser,
                     "browser/task-purpose", Transformation::kNone));
  }

  // The document language is page-authored, allowlisted, and marked as
  // normalized rather than as fact. It is the one thing in this adapter that
  // does come from the page, and it is not identity.
  const blink::WebElement root = document.DocumentElement();
  if (!root.IsNull()) {
    const std::string language = base::ToLowerASCII(
        Utf8(root.GetAttribute(blink::WebString::FromUtf8("lang"))));
    if (!language.empty() && language.size() <= 35) {
      // 35 is the longest well-formed language tag the IANA registry allows
      // for; anything longer is not a language tag and is not carried.
      node.attributes.emplace_back(AttributeKey::kLanguage, language);
      node.evidence.push_back(MakeEvidence(SemanticField::kAttributes,
                                           SourceKind::kDom, "dom/html-lang",
                                           Transformation::kNormalized));
    }
  }

  // Lifecycle, as the browser stated it. A document the browser has not
  // called active is not a normal observation, and saying so is the whole
  // point of carrying the browser's answer rather than this process's.
  const bool lifecycle_is_active =
      facts.lifecycle == DocumentLifecycle::kActive;
  if (!lifecycle_is_active) {
    node.states.push_back(NodeState::kBusy);
    node.evidence.push_back(
        MakeEvidence(SemanticField::kStates, SourceKind::kBrowser,
                     "browser/lifecycle", Transformation::kNone));
  }

  SemanticGraphStore::LiveNode live;
  live.node_id = id;
  live.dom_key =
      context.store->MakeKey(SemanticGraphStore::IdentitySpace::kDerived, 0);
  live.role = node.role;
  live.sensitivity = node.sensitivity;
  live.content_trust = node.content_trust;
  live.states = node.states;
  context.store->UpsertLiveNode(std::move(live));
  result.nodes.push_back(std::move(node));

  // The one comparison this process is uniquely placed to make.
  const OriginAgreement agreement =
      CompareOrigin(document, facts.allowed_origin_serializations);
  result.truncation = context.ledger->report();

  switch (agreement) {
    case OriginAgreement::kDisagrees:
      // Note what is NOT reported: the origin this process believes it has.
      // Emitting it would be a renderer asserting document identity, which
      // is the thing this module exists not to do. The broker knows its own
      // record and now knows the renderer disagrees with it, which is
      // everything it needs to invalidate.
      result.warnings.emplace_back(WarningCode::kAdapterFailed,
                                   "origin-outside-browser-allowed-set");
      result.status = AdapterStatus::kConflicted;
      return result;
    case OriginAgreement::kBrokerStatedNoSet:
      // The documented meaning of an empty allowlist is "the root frame's own
      // origin only", which this process cannot verify without asserting an
      // origin. Incomplete rather than ok: the check did not run.
      result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                   "no-browser-origin-set-to-compare");
      result.status = AdapterStatus::kIncomplete;
      return result;
    case OriginAgreement::kAgrees:
      break;
  }

  result.status = lifecycle_is_active && !result.truncation.truncated
                      ? AdapterStatus::kOk
                      : AdapterStatus::kIncomplete;
  return result;
}

}  // namespace taffy
