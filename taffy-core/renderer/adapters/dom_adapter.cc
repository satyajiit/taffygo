// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/dom_adapter.h"

#include <inttypes.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "taffy/renderer/adapters/bounded_web_text.h"
#include "taffy/renderer/adapters/dom_content_metadata.h"
#include "taffy/renderer/adapters/dom_role_mapping.h"
#include "taffy/renderer/adapters/dom_table_relationships.h"
#include "taffy/renderer/content_metadata.h"
#include "taffy/renderer/observed_link_limits.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "url/gurl.h"
#include "url/origin.h"

// VERIFY AT SP-04 - Blink public API surface used by this file. Each of these
// is a name this code depends on and that no host here can compile:
//   * blink::WebNode::GetDomNodeId() - the stable per-document DOM node
//     identifier. If it is not on WebNode at the pinned milestone, the
//     identity key in SemanticGraphStore::DomNodeKey must come from
//     blink::WebAXObject::AxID() instead, and the never-reuse test in
//     semantic_graph_store_unittest.cc still holds either way.
//   * blink::WebElement::TextContent() and its bounded variant. If only an
//     unbounded form exists, the traversal must charge the budget before the
//     call, not after, or a hostile page sets the peak memory.
//   * blink::WebElement::ShadowRoot() - only used to detect that a boundary
//     exists so the traversal can stop and record it. If the accessor is not
//     public, detection has to come from the accessibility adapter instead.
//   * blink::WebElement::HasHTMLAttribute() and blink::WebNode::To<T>() -
//     both are recent spellings of older calls (HasAttribute, WebElement::From)
//     and one of the two pairs is right at the pin. Answered at 152:
//     HasAttribute() is the survivor and To<T>() is current, so this file uses
//     both. See the download-attribute read below for why the two attribute
//     spellings ask the same question.
//   * Whether GetAttribute() on a detached element is safe. The traversal
//     never holds a node across a task, but a mutation observer firing
//     mid-traversal is exactly the hostile fixture case.

namespace taffy {

using dom_role_mapping::Attribute;
using dom_role_mapping::IsTextBearing;
using dom_role_mapping::LowerTagName;
using dom_role_mapping::RoleForTag;
using dom_role_mapping::Utf8;
using dom_table_relationships::TableCell;

namespace {

constexpr uint32_t kDomRuleVersion = 2;
constexpr char kAdapterName[] = "dom";

}  // namespace

DomAdapter::DomAdapter() = default;
DomAdapter::~DomAdapter() = default;

AdapterKind DomAdapter::kind() const {
  return AdapterKind::kDom;
}

std::string_view DomAdapter::name() const {
  return kAdapterName;
}

uint32_t DomAdapter::extraction_rule_version() const {
  return kDomRuleVersion;
}

AdapterResult DomAdapter::Run(ExtractionContext& context) {
  AdapterResult result;

  const blink::WebDocument document = context.frame->GetDocument();
  if (document.IsNull()) {
    result.status = AdapterStatus::kUnsupported;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "no-document");
    return result;
  }

  const blink::WebElement body = document.Body();
  if (body.IsNull()) {
    // A document with no body is not a failure; it is a document with nothing
    // for this adapter to say. Reporting kIncomplete rather than kOk keeps a
    // caller from reading "zero nodes" as "nothing on the page".
    result.status = AdapterStatus::kIncomplete;
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable, "no-body");
    return result;
  }

  const Sensitivity base_sensitivity =
      context.cross_origin_frame()
          ? Sensitivity::kUnknownSensitive
          : SensitivityClassifier::Stricter(Sensitivity::kNotSensitive,
                                            context.policy_floor());
  const ContentSignalLimits& signal_limits = context.limits->content_signals();
  const RendererContentTrust document_trust =
      content_metadata::DocumentTrust(context.cross_origin_frame());
  std::string document_language;
  const blink::WebElement document_element = document.DocumentElement();
  if (!document_element.IsNull()) {
    document_language = Attribute(document_element, "lang");
  }

  bool hit_shadow_boundary = false;
  bool hit_frame_boundary = false;
  bool saw_canvas = false;
  bool child_queue_truncated = false;
  std::vector<TableCell> table_cells;
  // Rows seen per table, so a cell's row index does not depend on having seen
  // the <tr> as a separate emitted node.
  std::map<int64_t, uint32_t> rows_per_table;

  // Iterative pre-order walk with an explicit stack. Recursion over a page's
  // DOM is a stack-depth attack a hostile fixture will absolutely try.
  struct Entry {
    blink::WebNode node;
    uint32_t depth;
    SemanticNodeId parent_id;
    bool has_parent;
    // List position bookkeeping, carried down so a list item can state where
    // it sits without a second pass.
    uint32_t list_position;
    uint32_t list_size;
    // Table bookkeeping. `table_key` is zero outside a table.
    int64_t table_key;
    uint32_t row_index;
    uint32_t column_index;
    RendererContentTrust content_trust =
        RendererContentTrust::kUnknownUntrusted;
    bool hidden_by_style = false;
    bool language_mismatch = false;
  };
  std::vector<Entry> stack;
  stack.push_back({body, 0u, SemanticNodeId(), false, 0u, 0u, 0, 0u, 0u,
                   document_trust, false, false});

  while (!stack.empty()) {
    const Entry current = stack.back();
    stack.pop_back();

    if (!context.ledger->CheckDeadline() || context.ledger->exhausted()) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      break;
    }
    if (!context.ledger->WithinDepth(current.depth)) {
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
      continue;
    }
    if (!current.node.IsElementNode()) {
      continue;
    }

    const blink::WebElement element = current.node.To<blink::WebElement>();
    const std::string tag = LowerTagName(element);
    const RendererContentTrust content_trust =
        dom_content_metadata::IsUserGeneratedRegion(element)
            ? content_metadata::UserGeneratedTrust(context.cross_origin_frame())
            : current.content_trust;
    const bool hidden_by_style = current.hidden_by_style ||
                                 dom_content_metadata::IsHiddenByStyle(element);
    const bool language_mismatch =
        current.language_mismatch ||
        content_metadata::AuthoredLanguagesDiffer(document_language,
                                                  Attribute(element, "lang"));
    const ContentSignalMask content_context = content_metadata::ContextSignals(
        hidden_by_style, language_mismatch, context.cross_origin_frame());

    // Never descend into script, style, template, or a frame's content. The
    // first two carry no user-perceivable semantics and are a classic place
    // to hide instructions for a model; the third belongs to another
    // endpoint.
    if (tag == "script" || tag == "style" || tag == "template" ||
        tag == "noscript") {
      continue;
    }
    if (tag == "iframe" || tag == "frame" || tag == "object" ||
        tag == "embed") {
      hit_frame_boundary = true;
      continue;
    }
    if (tag == "canvas") {
      // Protocol section 8.3: canvas pixels have no implicit semantics. The
      // element is recorded so a caller can see that content exists it cannot
      // read; nothing is inferred about what it contains.
      saw_canvas = true;
    }

    const SemanticRole role = RoleForTag(tag, element);
    SemanticNodeId node_id;
    bool emitted = false;

    // VERIFY AT SP-04: GetDomNodeId() name and availability, see file header.
    const int64_t dom_node_id = element.GetDomNodeId();

    if (role != SemanticRole::kUnknownContent || current.depth == 0) {
      if (!context.ledger->ChargeNode()) {
        context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        break;
      }
      node_id = context.store->AllocateOrLookup(context.store->MakeKey(
          SemanticGraphStore::IdentitySpace::kDom, dom_node_id));
      emitted = true;

      SemanticNode node;
      node.node_id = node_id;
      node.frame_id = context.store->frame_id();
      node.role = role;
      node.sensitivity = base_sensitivity;
      node.projection_path = ProjectionPath::kLightTree;
      node.confidence = 1.0;
      content_metadata::ApplyNodeContext(&node, content_trust, content_context);

      const std::string locator =
          base::StringPrintf("dom/%s/%" PRId64, tag.c_str(), dom_node_id);
      node.sources.push_back(SourceKind::kDom);
      node.evidence.push_back(MakeEvidence(SemanticField::kRole,
                                           SourceKind::kDom, locator,
                                           Transformation::kNone));

      if (IsTextBearing(role) &&
          MayEmitText(node.sensitivity, NodeTextClass::kContent)) {
        BoundedWebText bounded = BoundElementTextContent(
            element, *context.ledger,
            context.limits->snapshot().max_text_bytes());
        std::string text = std::move(bounded.text);
        // Second redaction line (protocol section 9): page text can contain a
        // secret the page put there itself. A match is dropped whole - a mask
        // would still state the length.
        if (!text.empty() &&
            !context.prohibited_value_filter.LooksLikeHighRiskIdentifier(
                text)) {
          TextRun run;
          run.text = std::move(text);
          run.source_kind = SourceKind::kDom;
          run.sensitivity = node.sensitivity;
          run.truncated = bounded.truncated;
          content_metadata::LabelTextRun(&run, &node, content_trust,
                                         content_context, signal_limits);
          node.text_runs.push_back(std::move(run));
          node.evidence.push_back(MakeEvidence(SemanticField::kTextRuns,
                                               SourceKind::kDom, locator,
                                               Transformation::kNone));
        }
      }

      // Allowlisted typed attributes only. Every one below has an
      // AttributeKey member; anything without one cannot be emitted at all.
      if (role == SemanticRole::kHeading) {
        node.attributes.emplace_back(AttributeKey::kHeadingLevel,
                                     tag.substr(1));
      }
      if (role == SemanticRole::kListItem && current.list_size > 0) {
        node.attributes.emplace_back(
            AttributeKey::kListPosition,
            base::NumberToString(current.list_position));
        node.attributes.emplace_back(AttributeKey::kListSize,
                                     base::NumberToString(current.list_size));
      }
      if (role == SemanticRole::kTableCell && current.table_key != 0) {
        node.attributes.emplace_back(AttributeKey::kTableRowIndex,
                                     base::NumberToString(current.row_index));
        node.attributes.emplace_back(
            AttributeKey::kTableColumnIndex,
            base::NumberToString(current.column_index));
        TableCell cell;
        cell.table_key = current.table_key;
        cell.row_index = current.row_index;
        cell.column_index = current.column_index;
        cell.is_header = tag == "th";
        cell.scope = base::ToLowerASCII(Attribute(element, "scope"));
        cell.node_id = node_id;
        table_cells.push_back(std::move(cell));
      }
      if (role == SemanticRole::kImage) {
        const std::string alt = Attribute(element, "alt");
        if (!alt.empty() &&
            !context.prohibited_value_filter.LooksLikeHighRiskIdentifier(alt)) {
          const std::string bounded_alt = context.ledger->BoundText(alt);
          node.attributes.emplace_back(AttributeKey::kMediaAltSource,
                                       bounded_alt);
          content_metadata::AddNodeTextSignals(&node, bounded_alt,
                                               content_context, signal_limits);
        }
      }

      if (role == SemanticRole::kLink) {
        // Resolve relative markup against this exact document while the node
        // is live. The browser parses and checks the canonical string again;
        // this resolution supplies an input, never an authorization verdict.
        const blink::WebString authored_href =
            element.GetAttribute(blink::WebString::FromUtf8("href"));
        if (authored_href.length() <=
            renderer::kMaxObservedLinkDestinationBytes) {
          const GURL resolved_href(document.CompleteURL(authored_href));
          if (resolved_href.is_valid() && resolved_href.SchemeIsHTTPOrHTTPS() &&
              !resolved_href.has_username() && !resolved_href.has_password() &&
              !resolved_href.spec().empty() &&
              resolved_href.spec().size() <=
                  renderer::kMaxObservedLinkDestinationBytes) {
            Destination destination;
            destination.url = resolved_href.spec();
            destination.is_cross_origin = url::Origin::Create(resolved_href) !=
                                          url::Origin::Create(document.Url());
            destination.opens_new_tab = base::EqualsCaseInsensitiveASCII(
                Attribute(element, "target"), "_blank");
            // Confirmed at the 152 pin: WebElement::HasHTMLAttribute() is gone
            // and WebElement::HasAttribute() is the surviving spelling.
            // Presence is the whole test: `download=""` is still a download.
            destination.is_download =
                element.HasAttribute(blink::WebString::FromUtf8("download"));
            node.destination = std::move(destination);
            node.evidence.push_back(MakeEvidence(SemanticField::kDestination,
                                                 SourceKind::kDom, locator,
                                                 Transformation::kNone));
          }
        }
        node.actions.push_back(ActionKind::kActivate);
        const std::string relation =
            base::ToLowerASCII(Attribute(element, "rel"));
        if (!relation.empty() && relation.size() <= 64) {
          node.attributes.emplace_back(AttributeKey::kLinkRelation, relation);
        }
      } else if (role == SemanticRole::kButton) {
        node.actions.push_back(ActionKind::kActivate);
      }
      node.actions.push_back(ActionKind::kScrollIntoView);

      SemanticGraphStore::LiveNode live;
      live.node_id = node_id;
      live.dom_key = context.store->MakeKey(
          SemanticGraphStore::IdentitySpace::kDom, dom_node_id);
      live.role = role;
      live.actions = node.actions;
      live.sensitivity = node.sensitivity;
      live.content_trust = node.content_trust;
      live.destination = node.destination;
      context.store->UpsertLiveNode(std::move(live));

      if (current.has_parent) {
        SemanticEdge edge;
        edge.from_frame_id = context.store->frame_id();
        edge.from_node_id = current.parent_id;
        edge.to_frame_id = context.store->frame_id();
        edge.to_node_id = node_id;
        edge.relationship = EdgeType::kContains;
        result.edges.push_back(std::move(edge));
      }
      result.nodes.push_back(std::move(node));
    }

    // Protocol section 8.2: stop at the boundary and say so. The graph is
    // incomplete without shadow content, and the accessibility adapter is
    // what fills it in.
    if (!element.ShadowRoot().IsNull()) {
      hit_shadow_boundary = true;
    }

    // Bookkeeping handed to the children.
    uint32_t child_list_size = 0;
    int64_t child_table_key = current.table_key;
    uint32_t child_row_index = current.row_index;
    if (tag == "table") {
      child_table_key = dom_node_id;
      child_row_index = 0;
      rows_per_table[child_table_key] = 0;
    } else if (tag == "tr" && current.table_key != 0) {
      child_row_index = rows_per_table[current.table_key]++;
    }

    // Children are pushed in reverse so that popping yields document order,
    // which is what makes the position indices above mean anything. Text
    // nodes are not queued: the owning element's bounded TextContent read has
    // already handled them, and the loop above can only emit elements.
    std::vector<blink::WebNode> children;
    const size_t unqueued_node_slots =
        context.ledger->RemainingNodes() > stack.size()
            ? context.ledger->RemainingNodes() - stack.size()
            : 0u;
    size_t omitted_children = 0;
    for (blink::WebNode child = element.FirstChild(); !child.IsNull();
         child = child.NextSibling()) {
      if (!context.ledger->CheckDeadline()) {
        context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        break;
      }
      if (!child.IsElementNode()) {
        continue;
      }
      const std::string child_tag = LowerTagName(child.To<blink::WebElement>());
      if ((tag == "ul" || tag == "ol") && child_tag == "li") {
        if (child_list_size < UINT32_MAX) {
          ++child_list_size;
        }
      }
      if (children.size() < unqueued_node_slots) {
        children.push_back(child);
      } else {
        ++omitted_children;
      }
    }
    if (omitted_children > 0) {
      child_queue_truncated = true;
      context.ledger->NoteOmittedNodes(omitted_children,
                                       /*could_change_answer=*/true);
    }
    uint32_t list_position = 0;
    uint32_t column_index = 0;
    for (blink::WebNode& child : children) {
      Entry entry;
      entry.node = child;
      entry.depth = current.depth + 1;
      entry.parent_id = emitted ? node_id : current.parent_id;
      entry.has_parent = emitted || current.has_parent;
      entry.list_size = child_list_size;
      entry.list_position = child_list_size > 0 ? list_position : 0;
      entry.table_key = child_table_key;
      entry.row_index = tag == "tr" ? child_row_index : current.row_index;
      entry.column_index = column_index;
      entry.content_trust = content_trust;
      entry.hidden_by_style = hidden_by_style;
      entry.language_mismatch = language_mismatch;
      if (child.IsElementNode()) {
        const std::string child_tag =
            LowerTagName(child.To<blink::WebElement>());
        if (child_tag == "li") {
          ++list_position;
        }
        if (child_tag == "td" || child_tag == "th") {
          ++column_index;
        }
      }
      stack.push_back(std::move(entry));
    }
    std::reverse(stack.end() - static_cast<std::ptrdiff_t>(children.size()),
                 stack.end());
  }

  dom_table_relationships::Append(table_cells, context, &result);

  result.truncation = context.ledger->report();

  if (hit_shadow_boundary) {
    result.warnings.emplace_back(WarningCode::kClosedShadowRootNotProjected,
                                 "dom-stops-at-shadow-boundary");
  }
  if (hit_frame_boundary) {
    result.warnings.emplace_back(WarningCode::kCrossOriginFrameOmitted,
                                 "dom-stops-at-frame-boundary");
  }
  if (saw_canvas) {
    result.warnings.emplace_back(WarningCode::kCanvasWithoutSemantics,
                                 "canvas-present");
  }
  if (child_queue_truncated) {
    result.warnings.emplace_back(WarningCode::kAdapterUnavailable,
                                 "dom-child-fanout-bound");
  }

  // A frame element is an exact endpoint boundary, not omitted content from
  // this frame's DOCUMENT scope. The browser owns frame topology and decides
  // separately which child-frame endpoints a grant may include. Keep the
  // warning so the boundary stays visible, but do not label this frame's
  // otherwise complete light-DOM answer incomplete merely because a child
  // document exists.
  const bool complete = !result.truncation.truncated && !hit_shadow_boundary &&
                        !child_queue_truncated;
  result.status = complete ? AdapterStatus::kOk : AdapterStatus::kIncomplete;
  return result;
}

}  // namespace taffy
