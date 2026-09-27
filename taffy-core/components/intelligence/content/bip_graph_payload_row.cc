// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/bip_graph_payload_row.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/memory/raw_ref.h"
#include "taffy/components/intelligence/content/bip_content_metadata_wire.h"
#include "taffy/components/intelligence/content/bip_graph_payload.h"

namespace taffy::bip_payload {
namespace {

uint8_t Bit(BipNodeFlag flag) {
  return static_cast<uint8_t>(flag);
}

uint8_t Bit(BipDestinationFlag flag) {
  return static_cast<uint8_t>(flag);
}

uint8_t Bit(BipTextRunFlag flag) {
  return static_cast<uint8_t>(flag);
}

// The first `ceiling` bytes of `text`, cut on a UTF-8 boundary.
//
// The ceilings in `bip_graph_payload.h` bound the browser's *second pass* over
// a renderer-authored string: they exist so one overlong row cannot turn into
// unbounded rescanning here. Bounding the input serves that exactly, and
// refusing the row does not serve it any better — it only loses the page. It
// lost a Google results page on a phone, as `result code 10, nodes 1193,
// encoding 0, graph bytes 0`: one node's accessible name was longer than a
// kilobyte, the whole observation was called a browser defect, and the model
// never saw a single result (decision 0172).
std::string_view BoundedField(std::string_view text, size_t ceiling) {
  if (text.size() <= ceiling) {
    return text;
  }
  size_t cut = ceiling;
  // A UTF-8 continuation byte is 10xxxxxx. Walking back to the start of the
  // character being split cannot pass the front, because the first byte of a
  // sequence is never a continuation byte.
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  return text.substr(0, cut);
}

// True when this node's name may cross. The rule is fail-closed on purpose:
// anything but kNotSensitive withholds, so a sensitivity the browser could not
// classify (kUnknownSensitive) withholds too.
bool NameMayCross(const mojom::SemanticNode& node) {
  if (node.sensitivity != mojom::Sensitivity::kNotSensitive) {
    return false;
  }
  if (node.value_descriptor &&
      node.value_descriptor->kind == mojom::ValueKind::kSecretWithheld) {
    return false;
  }
  return true;
}

// True when a state says what the node's value currently is, rather than what
// kind of node it is or whether it can be acted on.
//
// The distinction is the whole of the rule below it. "Visible", "enabled" and
// "required" are properties of a control; "checked" and "selected" are its
// answer. On a page that asks a question the person would not want repeated,
// the tick IS the sensitive datum, and a framing that withheld the label while
// carrying the tick would have withheld the part that was not secret.
bool StateDescribesTheValue(mojom::NodeState state) {
  switch (state) {
    case mojom::NodeState::kChecked:
    case mojom::NodeState::kUnchecked:
    case mojom::NodeState::kMixed:
    case mojom::NodeState::kSelected:
      return true;
    case mojom::NodeState::kVisible:
    case mojom::NodeState::kNotVisible:
    case mojom::NodeState::kOffscreen:
    case mojom::NodeState::kObscured:
    case mojom::NodeState::kEnabled:
    case mojom::NodeState::kDisabled:
    case mojom::NodeState::kEditable:
    case mojom::NodeState::kReadOnly:
    case mojom::NodeState::kRequired:
    case mojom::NodeState::kInvalid:
    case mojom::NodeState::kExpanded:
    case mojom::NodeState::kCollapsed:
    case mojom::NodeState::kFocused:
    case mojom::NodeState::kBusy:
      return false;
  }
  return true;
}

InspectorNodeRole ToInspectorRole(mojom::SemanticRole role) {
  switch (role) {
    case mojom::SemanticRole::kDocument:
      return InspectorNodeRole::kDocument;
    case mojom::SemanticRole::kRegion:
    case mojom::SemanticRole::kHeading:
      return InspectorNodeRole::kSection;
    case mojom::SemanticRole::kParagraph:
    case mojom::SemanticRole::kListItem:
      return InspectorNodeRole::kText;
    case mojom::SemanticRole::kList:
      return InspectorNodeRole::kList;
    case mojom::SemanticRole::kTable:
    case mojom::SemanticRole::kTableRow:
    case mojom::SemanticRole::kTableCell:
      return InspectorNodeRole::kTable;
    case mojom::SemanticRole::kLink:
      return InspectorNodeRole::kLink;
    case mojom::SemanticRole::kButton:
    case mojom::SemanticRole::kSearchField:
    case mojom::SemanticRole::kTextField:
    case mojom::SemanticRole::kCheckbox:
    case mojom::SemanticRole::kRadio:
    case mojom::SemanticRole::kSelect:
    case mojom::SemanticRole::kOption:
    case mojom::SemanticRole::kUnknownInteractive:
      return InspectorNodeRole::kControl;
    case mojom::SemanticRole::kImage:
    case mojom::SemanticRole::kMedia:
      return InspectorNodeRole::kMedia;
    case mojom::SemanticRole::kPrice:
    case mojom::SemanticRole::kRating:
    case mojom::SemanticRole::kAvailability:
      return InspectorNodeRole::kCommerce;
    case mojom::SemanticRole::kLabelValuePair:
    case mojom::SemanticRole::kCitation:
      return InspectorNodeRole::kReference;
    case mojom::SemanticRole::kUnknownContent:
      return InspectorNodeRole::kUnknown;
  }
  return InspectorNodeRole::kUnknown;
}

InspectorSensitivity ToInspectorSensitivity(mojom::Sensitivity sensitivity) {
  switch (sensitivity) {
    case mojom::Sensitivity::kNotSensitive:
      return InspectorSensitivity::kPublic;
    case mojom::Sensitivity::kUnknownSensitive:
      return InspectorSensitivity::kUnknown;
    case mojom::Sensitivity::kPersonal:
    case mojom::Sensitivity::kAccount:
    case mojom::Sensitivity::kPayment:
    case mojom::Sensitivity::kIdentity:
    case mojom::Sensitivity::kHealth:
    case mojom::Sensitivity::kFinancial:
    case mojom::Sensitivity::kLegal:
    case mojom::Sensitivity::kPrivateCommunication:
    case mojom::Sensitivity::kAdministration:
    case mojom::Sensitivity::kCredential:
    case mojom::Sensitivity::kOneTimeCode:
    case mojom::Sensitivity::kChallengeResponse:
      return InspectorSensitivity::kWithheld;
  }
  return InspectorSensitivity::kUnknown;
}

}  // namespace

InspectorRelationship ToInspectorRelationship(
    mojom::RelationshipKind relationship) {
  switch (relationship) {
    case mojom::RelationshipKind::kContains:
    case mojom::RelationshipKind::kOwns:
      return InspectorRelationship::kHierarchy;
    case mojom::RelationshipKind::kLabels:
      return InspectorRelationship::kLabel;
    case mojom::RelationshipKind::kDescribes:
      return InspectorRelationship::kDescription;
    case mojom::RelationshipKind::kControls:
      return InspectorRelationship::kControl;
    case mojom::RelationshipKind::kRowHeaderFor:
    case mojom::RelationshipKind::kColumnHeaderFor:
      return InspectorRelationship::kTableHeader;
    case mojom::RelationshipKind::kSameEntityAs:
      return InspectorRelationship::kEntity;
    case mojom::RelationshipKind::kSourceFor:
      return InspectorRelationship::kSource;
  }
  return InspectorRelationship::kOther;
}

// One semantic node row. Shared by the snapshot and the delta so that a
// consumer decodes one shape, and so that the withholding rule above and the
// rescan below cannot be applied on one path and forgotten on the other.
[[nodiscard]] bool WriteNode(Writer& out,
                             const mojom::SemanticNode& node,
                             const FrameId& frame_id,
                             RescanTally* tally,
                             size_t& remaining_text_bytes,
                             std::string display_id,
                             InspectorNodeProjection* projection,
                             bool* text_budget_exhausted) {
  // These are schema bounds, enforced before any list is walked or string is
  // rescanned. Mojo has already allocated the message, but a compromised
  // renderer must not turn one overlong row into an unbounded second pass in
  // the browser process.
  //
  // Only the bounds that cannot be met by carrying less are fatal, and the
  // distinction is the difference between a page and no page at all. An
  // identity cannot be shortened — a truncated identifier names a different
  // node — and a closed enumeration cannot have more members than it has, so a
  // longer action or state list means the canonicalisation did not run. A
  // string that is too long, and a text-run list that is too long, are the
  // page being bigger than the wire: those are bounded below rather than
  // refused here (decision 0172).
  const char* refused = nullptr;
  if (node.node_id.empty() || node.node_id.size() > kMaxBipIdentityBytes) {
    refused = "node-identity";
  } else if (node.frame_id.empty() ||
             node.frame_id.size() > kMaxBipIdentityBytes) {
    refused = "frame-identity";
  } else if (node.actions.size() > kMaxBipNodeActions) {
    refused = "actions";
  } else if (node.states.size() > kMaxBipNodeStates) {
    refused = "states";
  }
  if (refused) {
    // One row refuses the whole payload, and the payload then reaches the task
    // as an internal error, so which row and why is the only fact that can
    // tell a renderer defect from a page this build cannot carry. Every value
    // is a compiled-in name or a count.
    LOG(WARNING) << "[taffy_graph_row_refused] at=" << refused
                 << " actions=" << node.actions.size()
                 << " states=" << node.states.size();
    return false;
  }
  const auto charge_text = [&remaining_text_bytes,
                            text_budget_exhausted](std::string_view text) {
    if (text.size() > remaining_text_bytes) {
      if (text_budget_exhausted) {
        *text_budget_exhausted = true;
      }
      return false;
    }
    remaining_text_bytes -= text.size();
    return true;
  };
  // Charge what arrived, not only what disclosure permits the payload to keep.
  // Otherwise a compromised renderer can hide arbitrarily much work behind a
  // sensitive label and consume no encoded-byte budget for it.
  if ((node.name.has_value() && !charge_text(*node.name)) ||
      (node.description.has_value() && !charge_text(*node.description))) {
    return false;
  }
  const std::optional<uint8_t> node_authorship =
      RendererAuthorship(node.content_trust);
  if (!node_authorship.has_value() ||
      !ContentSignalsAreCanonical(node.content_signals)) {
    return false;
  }
  const bool name_crosses = NameMayCross(node);
  // The same question, asked of the states rather than of the name, and
  // answered by the same rule for the same reason. A node whose label is too
  // sensitive to carry is a node whose answer is too sensitive to carry.
  const bool value_states_cross = name_crosses;
  bool withheld_a_value_state = false;
  for (const mojom::NodeState state : node.states) {
    if (StateDescribesTheValue(state)) {
      if (!value_states_cross) {
        withheld_a_value_state = true;
      }
    }
  }

  // --- version 3 -----------------------------------------------------------
  //
  // Which runs may cross, decided here because the answer is part of the flags
  // byte written below.
  //
  // Two conditions, both fail-closed. The node must be one whose name may
  // cross — a node too sensitive to label is too sensitive to quote, and the
  // text of a sensitive control is generally *more* revealing than its label,
  // not less. And the run itself must be marked not sensitive: a run carries
  // its own sensitivity because a page can put a card number inside an
  // otherwise ordinary paragraph, and the node-level answer would miss it.
  //
  // Then the same second layer a name gets. The sensitivity above was written
  // by the renderer, which is inside the sandbox and is exactly the party a
  // compromised page controls, so every byte that survives the label test is
  // rescanned here by shape before it is written. What that pass removes is
  // reported through the tally rather than quietly repaired, because a
  // non-empty tally means the renderer's own layer failed and that is a fact
  // somebody needs to see.
  struct EmittedRun {
    std::string text;
    uint8_t source_kind = 0;
    uint8_t sensitivity = 0;
    uint8_t flags = 0;
    uint8_t content_trust = 0;
    const raw_ref<const std::vector<mojom::ContentSignal>> content_signals;
  };
  std::vector<EmittedRun> emitted_runs;
  bool withheld_text = false;
  bool emission_stopped = false;
  size_t emitted_text_bytes = 0;
  // A node with more runs than the schema carries keeps the ones that fit and
  // says so, rather than taking the page down with it. The ceiling is here to
  // bound the rescanning below, and bounding the loop bounds it.
  size_t scanned_runs = 0;
  for (const auto& run : node.text_runs) {
    if (!run) {
      LOG(WARNING) << "[taffy_graph_row_refused] at=null-text-run";
      return false;
    }
    if (scanned_runs >= kMaxBipNodeTextRuns) {
      withheld_text = true;
      break;
    }
    ++scanned_runs;
    // Charged before the length test, so a renderer cannot hide work behind a
    // run this build will not carry.
    if (!charge_text(run->text)) {
      return false;
    }
    if (run->text.size() > kMaxBipTextRunBytes) {
      // One run too long for the wire is one run withheld. Stopping here
      // rather than skipping it is the rule two branches down: a consumer
      // reads a node's runs as prose in document order.
      withheld_text = true;
      emission_stopped = true;
      continue;
    }
    const std::optional<uint8_t> run_authorship =
        RendererAuthorship(run->content_trust);
    if (!run_authorship.has_value() ||
        !ContentSignalsAreCanonical(run->content_signals)) {
      return false;
    }
    if (emission_stopped || !name_crosses ||
        run->sensitivity != mojom::Sensitivity::kNotSensitive) {
      withheld_text = true;
      continue;
    }
    std::string safe = RescanRendererText(run->text, tally);
    if (safe.size() > kMaxBipNodeTextBytes - emitted_text_bytes) {
      // Stop at the first run that does not fit rather than skipping it and
      // taking a later shorter one. A consumer reads a node's text as prose in
      // document order, and a gap in the middle reads as a sentence rather than
      // as an omission.
      withheld_text = true;
      emission_stopped = true;
      continue;
    }
    emitted_text_bytes += safe.size();
    uint8_t run_flags = 0;
    if (run->truncated) {
      run_flags |= Bit(BipTextRunFlag::kTruncated);
    }
    emitted_runs.push_back(EmittedRun{
        .text = std::move(safe),
        .source_kind = static_cast<uint8_t>(run->source_kind),
        .sensitivity = static_cast<uint8_t>(run->sensitivity),
        .flags = run_flags,
        .content_trust = *run_authorship,
        .content_signals = raw_ref<const std::vector<mojom::ContentSignal>>(
            run->content_signals),
    });
  }

  uint8_t flags = 0;
  if (withheld_text) {
    flags |= Bit(BipNodeFlag::kTextWithheld);
  }
  if (node.name.has_value() && !name_crosses) {
    flags |= Bit(BipNodeFlag::kNameWithheld);
  }
  if (withheld_a_value_state) {
    flags |= Bit(BipNodeFlag::kValueStatesWithheld);
  }
  if (node.value_descriptor) {
    if (node.value_descriptor->present) {
      flags |= Bit(BipNodeFlag::kValuePresent);
    }
    if (node.value_descriptor->redacted) {
      flags |= Bit(BipNodeFlag::kValueRedacted);
    }
    if (node.value_descriptor->kind == mojom::ValueKind::kSecretWithheld) {
      flags |= Bit(BipNodeFlag::kSecretWithheld);
    }
  }

  out.Short(node.node_id);
  // The browser's identity for this document, never the renderer's echo of
  // it. `node.frame_id` holds a value the renderer minted for itself and it
  // is discarded here: it belongs to a namespace the browser does not share,
  // so carrying it would name a frame no consumer could resolve, and honouring
  // it would let a renderer put another frame's identity on a node. The
  // isolated core checks every row against the envelope's frame and refuses
  // the payload on a mismatch, which is the check this write is the other
  // half of.
  out.Short(frame_id.value);
  out.U16(static_cast<uint16_t>(node.role));
  out.U8(static_cast<uint8_t>(node.sensitivity));
  out.U8(flags);
  out.U8(node.value_descriptor
             ? static_cast<uint8_t>(node.value_descriptor->kind)
             : static_cast<uint8_t>(mojom::ValueKind::kUnknown));
  // The node's own label, rescanned. The sensitivity written above said this
  // name was safe to carry; that is a claim from inside the sandbox, and the
  // rescan is what makes the claim survivable when it is a lie. A name with
  // nothing in it comes back byte for byte, so an honest renderer pays a scan
  // and nothing else.
  //
  // Until version 3 this was the only renderer-authored string in the payload
  // and the comment here said so. It is now one of two, the other being the
  // text runs selected above, and both go through the same pass. If a third
  // ever crosses it goes through it too — the rule is that no page-authored
  // string reaches the isolated core without it, not that names are special.
  //
  // Bounded before the rescan rather than refused before the row: the ceiling
  // exists to keep this pass finite, and a prefix keeps it finite. A name
  // longer than a kilobyte is already a page doing something unusual, and the
  // first kilobyte of it is what a reader would have used.
  const std::string safe_name =
      name_crosses && node.name.has_value()
          ? RescanRendererText(BoundedField(*node.name, kMaxBipNodeNameBytes),
                               tally)
          : std::string();
  out.Short(safe_name);

  // How much text the page had, whatever this payload goes on to carry of it.
  // These two are the *original* measurements and stay that way: a consumer
  // comparing them against what it received is how it learns that something was
  // withheld and roughly how much, and a count rewritten to match what crossed
  // would answer that question with a lie.
  out.Count(node.text_runs.size());
  uint64_t text_bytes = 0;
  for (const auto& run : node.text_runs) {
    if (!run) {
      return false;
    }
    const uint64_t run_bytes = run->text.size();
    text_bytes = run_bytes > std::numeric_limits<uint64_t>::max() - text_bytes
                     ? std::numeric_limits<uint64_t>::max()
                     : text_bytes + run_bytes;
  }
  out.U64(text_bytes);

  // --- version 2 ------------------------------------------------------------
  //
  // What the node can be asked to do. This grants nothing: an action listed
  // here is a claim by an adapter about what the element would respond to, and
  // every one of them still has to be authorized, minted as a capability and
  // checked against the milestone before anything is dispatched. Carrying it
  // is what lets a consumer offer the right verb rather than guess one from a
  // role.
  //
  // Refused rather than truncated when a node claims more than the
  // enumeration has members, because a canonicalised list cannot be longer
  // than that and a longer one means the canonicalisation did not run.
  out.Count(node.actions.size());
  for (const mojom::ActionType action : node.actions) {
    out.U16(static_cast<uint16_t>(action));
  }

  // The states that survived the rule above. The count is written from what is
  // actually emitted rather than from what arrived, so a consumer never reads
  // a length it cannot fill.
  size_t emitted_states = 0;
  for (const mojom::NodeState state : node.states) {
    if (value_states_cross || !StateDescribesTheValue(state)) {
      ++emitted_states;
    }
  }
  out.Count(emitted_states);
  for (const mojom::NodeState state : node.states) {
    if (value_states_cross || !StateDescribesTheValue(state)) {
      out.U8(static_cast<uint8_t>(state));
    }
  }

  // The class of place a destination leads to, and never the place. There is
  // no URL in this framing and no field one could be put in — not the origin,
  // not the path, not a digest. A consumer that needs to decide whether
  // following this link is the same kind of act as scrolling has what it needs
  // in four bits; a consumer that wants the address is asking the wrong layer.
  uint8_t destination_flags = 0;
  if (node.destination) {
    destination_flags |= Bit(BipDestinationFlag::kPresent);
    if (node.destination->is_cross_origin) {
      destination_flags |= Bit(BipDestinationFlag::kCrossOrigin);
    }
    if (node.destination->opens_new_tab) {
      destination_flags |= Bit(BipDestinationFlag::kOpensNewTab);
    }
    if (node.destination->is_download) {
      destination_flags |= Bit(BipDestinationFlag::kIsDownload);
    }
  }
  out.U8(destination_flags);

  // --- version 4 -----------------------------------------------------------
  //
  // The authorship and injection evidence already present on the generated
  // BIP snapshot. These values used to stop here: framing v3 copied the text
  // and silently lost the facts that said who wrote it and what the traversal
  // found. The isolated core therefore saw a concealed imperative sentence as
  // ordinary page prose. Both are carried as closed values now, and neither
  // is allowed to authorize anything downstream.
  out.U8(*node_authorship);
  WriteContentSignals(out, node.content_signals);

  // The text itself. The count is what is actually here, not what the page
  // had — the page's own count and byte total were written above and are the
  // thing this is compared against.
  //
  // Each run carries the sensitivity it crossed under even though every one of
  // them is, by the rule above, not sensitive. It is a byte per run buying the
  // decoder a check it could not otherwise make: a payload from anything but
  // this encoder that puts sensitive text in a run is refused on the far
  // side rather than believed. Same discipline as the frame identity, and it
  // is there for the same reason.
  out.Count(emitted_runs.size());
  for (const EmittedRun& run : emitted_runs) {
    out.Short(run.text);
    out.U8(run.source_kind);
    out.U8(run.sensitivity);
    out.U8(run.flags);
    out.U8(run.content_trust);
    WriteContentSignals(out, *run.content_signals);
  }

  if (projection) {
    projection->display_id = std::move(display_id);
    projection->role = ToInspectorRole(node.role);
    if (!safe_name.empty() && safe_name.size() <= 512u) {
      projection->name = safe_name;
    }
    projection->sensitivity = ToInspectorSensitivity(node.sensitivity);
    projection->text_run_count =
        node.text_runs.size() > std::numeric_limits<uint32_t>::max()
            ? std::numeric_limits<uint32_t>::max()
            : static_cast<uint32_t>(node.text_runs.size());
    projection->text_byte_count = text_bytes;
    projection->value_present =
        node.value_descriptor && node.value_descriptor->present;
    projection->value_withheld =
        node.value_descriptor &&
        (node.value_descriptor->redacted ||
         node.value_descriptor->kind == mojom::ValueKind::kSecretWithheld);
  }
  return out.ok();
}

// One edge row, same reasoning.
[[nodiscard]] bool WriteEdge(Writer& out, const mojom::SemanticEdge& edge) {
  if (edge.from_frame_id.empty() ||
      edge.from_frame_id.size() > kMaxBipIdentityBytes ||
      edge.from_node_id.empty() ||
      edge.from_node_id.size() > kMaxBipIdentityBytes ||
      edge.to_frame_id.empty() ||
      edge.to_frame_id.size() > kMaxBipIdentityBytes ||
      edge.to_node_id.empty() ||
      edge.to_node_id.size() > kMaxBipIdentityBytes) {
    return false;
  }
  out.Short(edge.from_node_id);
  out.Short(edge.to_node_id);
  out.U16(static_cast<uint16_t>(edge.relationship));
  out.U8(edge.inferred ? 1u : 0u);
  return out.ok();
}

}  // namespace taffy::bip_payload
