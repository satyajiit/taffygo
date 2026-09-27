// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_BIP_GRAPH_PAYLOAD_H_
#define TAFFY_BROWSER_BIP_GRAPH_PAYLOAD_H_

#include <stdint.h>

#include <cstddef>
#include <string>
#include <vector>

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/components/intelligence/content/renderer_text_rescan.h"

// Encodes only the semantic graph body defined by the generated BIP contract.
// The Core Service envelope is generated Mojo and carries observation code,
// frame/document identity, revision, and recovery metadata as typed fields;
// this file is not a second browser-to-core observation frame.
//
// Text content crosses only for a node the browser has already established is
// not sensitive. `EncodeGraphPayload` withholds the name of any node whose
// sensitivity is anything other than kNotSensitive, and of any node whose
// value descriptor says a secret was withheld, and marks the node so the
// core can tell "no name" from "name withheld". No password, OTP, payment
// value or passkey can reach the isolated core through this framing, and the
// reason is structural rather than a filter that has to be remembered.
//
// That rule reads a label the renderer wrote, which is why it is not the only
// rule. A compromised renderer labels a node kNotSensitive and puts a secret
// in its name; nothing in the message is malformed and Mojo has nothing to
// reject. Every name that does cross is therefore rescanned here by the
// browser's own shape-based pass (renderer_text_rescan.h) before it is
// written, and what that pass removed is reported through the tally so a
// caller can say the renderer's layer failed rather than quietly repairing
// it. Withholding by label and removing by shape are independent layers on
// purpose: neither is the other's fallback.

namespace taffy {

namespace mojom {
class PageDelta;
class PageSnapshot;
}  // namespace mojom

// Version 2 carries, per node, what the node can be asked to do, what state it
// is in, and what class of place its destination leads to. Version 1 carried
// none of the three, which meant nothing downstream could offer a person or a
// model a control to act on: an ordinal cannot be bound to a node whose
// available actions are unknown, an unfilled required field cannot be
// distinguished from a filled one, and a link that starts a download cannot be
// told from one that scrolls the page.
//
// Version 3 carries the text runs themselves, where version 2 carried only how
// many there were and how many bytes they held. That is a reversal of an
// earlier decision and it is made deliberately: a summary is enough to say a
// page is large and not enough to say anything about what it contains, so
// everything built on top of it — a projection a person or a model can read, a
// question answered from a page, a citation — was unreachable. Two counts
// describe a document nobody can act on (decision 0056).
//
// What changed to make it safe is not the sensitivity rule, which is the same
// rule names have always followed, but that the rule now runs twice over text
// as it already ran twice over a name: withheld by the label the renderer
// wrote, and then rescanned by shape in the browser because that label came
// from inside the sandbox. Neither layer is the other's fallback.
//
// Version 4 carries the authorship label and content signals the generated BIP
// contract already places on every node and text run. Version 3 silently
// dropped both while framing the graph for Rust. That made protocol 0.4's
// injection evidence disappear at the exact boundary it was added for: the
// renderer could detect a concealed instruction, the snapshot could name it,
// and the model still received indistinguishable plain text.
//
// An absent authorship label is encoded as kUnknownUntrusted, never as a zero
// default. A renderer is not allowed to mint kUserAuthored, kTaffyAuthored or
// kModelAuthored; meeting any of the three refuses the whole payload. Signals
// remain a bounded list of closed-enumeration values rather than becoming a
// mask at this second boundary, for the same fail-closed reason they are an
// array in Mojo.
inline constexpr uint8_t kBipGraphPayloadFraming = 4;
inline constexpr uint8_t kBipDeltaPayloadFraming = 4;

inline constexpr size_t kMaxBipGraphPayloadBytes = 4u * 1024u * 1024u;
inline constexpr size_t kMaxBipIdentityBytes = 128;
// The two renderer-authored strings this payload may carry, and the bound on
// each. These cap the browser's own second pass over a string a sandboxed
// process wrote; a row that exceeds one is carried with that field bounded
// rather than refused, because refusing it loses the whole page and bounds no
// more work than bounding the field does (decision 0172).
inline constexpr size_t kMaxBipNodeNameBytes = 1024;
inline constexpr size_t kMaxBipTextRunBytes = 4096;

// Per-node bounds on the two lists version 2 added. Both are canonicalised
// upstream — sorted and made unique in the renderer's graph store — so the
// honest bound is the size of the enumeration itself, and anything longer is a
// defect rather than a large page.
inline constexpr size_t kMaxBipNodeActions = 7;
inline constexpr size_t kMaxBipNodeStates = 18;

// Per-node bounds on the text version 3 added.
//
// The byte bound is per node rather than per run because a run boundary is a
// page's choice and a node is the unit a consumer reasons about. A node that
// runs past it keeps the runs that fit, whole, and reports the rest as
// withheld: half a sentence is worse than a marked absence, and the flag tells
// a consumer to ask for more rather than conclude there was none.
//
// Eight kilobytes is comfortably above an ordinary paragraph and comfortably
// below the isolated core's own total (65,536 bytes across an entire
// observation), so a single pathological node cannot spend the document's
// whole budget.
inline constexpr size_t kMaxBipNodeTextRuns = 512;
inline constexpr size_t kMaxBipNodeTextBytes = 8u * 1024u;

// The schema permits at most eight findings on one node or run. There are
// seven members today; keeping the schema's bound here means an additive
// member does not require a second numeric decision, while the closed enum on
// each item still makes an older reader refuse the new value.
inline constexpr size_t kMaxBipContentSignals = 8;

// Flags on one semantic node.
enum class BipNodeFlag : uint8_t {
  // The node has a name and this framing is not carrying it, because the
  // node is sensitive. Distinct from "the node has no name", which is the
  // absence of this flag together with a zero-length name.
  kNameWithheld = 1 << 0,
  kValuePresent = 1 << 1,
  kValueRedacted = 1 << 2,
  kSecretWithheld = 1 << 3,
  // The node had at least one state describing what its value currently is —
  // checked, selected, or the mixed state between them — and this framing is
  // not carrying it. For a sensitive control that state IS the value: whether
  // a box labelled with a diagnosis is ticked is the answer, not a property of
  // the control. Structural states cross for the same node, because a
  // precondition has to be able to ask whether it is visible and enabled.
  //
  // As with kNameWithheld, the flag exists so that "withheld" and "not in that
  // state" cannot decode to the same thing.
  kValueStatesWithheld = 1 << 4,
  // The node had text and at least one run of it is not in this payload —
  // because the node is sensitive, because the run itself was, or because the
  // node's byte bound was reached. As with kNameWithheld, the flag exists so
  // that "withheld" and "there was none" cannot decode to the same thing: a
  // consumer told nothing concludes the page does not say it, and goes looking
  // again for something it has already been refused.
  kTextWithheld = 1 << 5,
};

// Flags on one text run.
enum class BipTextRunFlag : uint8_t {
  // The renderer already shortened this run before the browser saw it. Carried
  // rather than inferred, because a consumer deciding whether it has read a
  // whole sentence cannot tell from the bytes.
  kTruncated = 1 << 0,
};

// Flags on one node's destination, carried instead of the destination. The URL
// itself never crosses this framing in any form — not the origin, not the
// path, not a digest of it. What a consumer needs in order to decide whether
// following a link is the same kind of act as scrolling is the class of place
// it leads to, and these four bits are that class.
enum class BipDestinationFlag : uint8_t {
  kPresent = 1 << 0,
  kCrossOrigin = 1 << 1,
  kOpensNewTab = 1 << 2,
  kIsDownload = 1 << 3,
};

// Encodes the semantic graph of one validated renderer snapshot. This is the
// body of BipGraphPayloadEncoder (graph_payload_encoder.h), exposed so it can
// be tested against a snapshot without a broker.
//
// `frame_id` is the browser's own identity for the document this snapshot came
// from, and it is written onto every node row in place of whatever the
// renderer stamped there. It is a parameter rather than a field read off the
// snapshot because the renderer's frame identity is not the broker's: a
// renderer mints its own local value (taffy_render_frame_observer.h) and a
// renderer that could choose a browser-space one could put another frame's
// identity on a node and have the core act on it. One snapshot describes
// exactly one document — the renderer walks no child frames — so one
// identity is the whole answer and the rewrite is total rather than a lookup
// that could miss a row.
//
// `tally`, when supplied, receives what the browser's own redaction pass
// removed from the names that crossed. A caller that passes nullptr still gets
// the removal — the pass is not optional — it just cannot report it.
// `max_bytes` is enforced while fields are appended and is also capped at the
// framing-wide emergency ceiling. Refusal returns an empty vector; no prefix is
// returned as a decodable partial graph.
// `max_text_bytes` charges the original names, descriptions, and runs,
// including fields withheld by the disclosure rule, and therefore enforces
// the request budget independently of the total encoded-byte ceiling.
// `did_not_fit`, when supplied, is set true exactly when the refusal was the
// byte ceiling and not a malformed input. Both return an empty vector, because
// neither may produce a decodable partial graph, but they are different things
// to tell the person waiting: one is a page too large for the budget it was
// observed under, which is answerable by asking for less, and the other is a
// renderer reply that does not conform, which is not.
std::vector<uint8_t> EncodeGraphPayload(
    const mojom::PageSnapshot& snapshot,
    const FrameId& frame_id,
    RescanTally* tally = nullptr,
    InspectorGraphProjection* projection = nullptr,
    size_t max_bytes = kMaxBipGraphPayloadBytes,
    size_t max_text_bytes = kMaxBipGraphPayloadBytes,
    bool* did_not_fit = nullptr);

// The same for one delta, and `frame_id` means the same thing and is written
// in the same two places: the delta envelope and every added node's row.
// Node identity and structure cross; a changed node's body does not.
// `max_bytes` has the same fail-closed meaning as for a snapshot and also caps
// original delta text that is withheld and therefore consumes no output bytes.
std::vector<uint8_t> EncodeDeltaPayload(
    const mojom::PageDelta& delta,
    const FrameId& frame_id,
    RescanTally* tally = nullptr,
    size_t max_bytes = kMaxBipGraphPayloadBytes);

}  // namespace taffy

#endif  // TAFFY_BROWSER_BIP_GRAPH_PAYLOAD_H_
