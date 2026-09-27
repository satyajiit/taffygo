// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_TEXT_RESCAN_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_TEXT_RESCAN_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <type_traits>

// The browser process's own redaction pass over renderer-supplied free text
// (PAR-SEC-009, REQ-DATA-003, threat model adversary A2).
//
// # Why this exists
//
// Protocol section 9 opens by saying redaction occurs in layers "because no
// single detector is sufficient", and names the browser broker as the second
// of them. Layer 1 — omitting a prohibited field value before it is ever read
// — runs in the renderer, inside the traversal that can see the DOM. That is
// the right place for it and the only place it can be. It is also inside the
// sandbox, which means adversary A2, a compromised renderer that speaks the
// protocol correctly, can simply not run it. Every field it fills is well
// formed, so Mojo has nothing to reject, and a browser that copied the reply
// onward would be relying on the honesty of the process it is defending
// against.
//
// Layer 2 was applying policy — origin, task, incognito, enterprise — and
// nothing else, so a value that policy admitted crossed on the renderer's word
// alone. This is the rest of layer 2: the browser's own detector, running on
// what it is about to hand onward. It is the same rule the last layer already
// follows, where audit-engine assumes every layer above it failed and
// re-redacts. A value a renderer volunteers is still a secret, and this pass
// is what makes a renderer skipping its own redaction a bounded failure rather
// than a disclosure.
//
// # What it is not
//
// It is not a replacement for the renderer's field redaction, and it cannot
// be: it sees text, not a DOM, so it cannot know that a value came from a
// password input. It recognises shapes (secret_shape_scanner.h) — a bearer
// token, a certificate block, a labelled parameter, a Luhn-passing digit run,
// a fixture canary. A first layer that omits by provenance and a second that
// removes by shape catch different things on purpose, and neither is the
// other's fallback.
//
// # Free text only
//
// This is applied to text a renderer authored and the browser hands onward:
// an accessible name, an adapter's diagnostic code. It is deliberately *not*
// applied to identifiers. A node identifier is a handle the browser echoes
// back to the renderer to resolve a node; rewriting one would break the
// round-trip that every action depends on, so identifiers are bounded and
// checked against browser-owned state rather than scrubbed.

namespace taffy {

// What one rescan removed. Counts and nothing else: a report that carried the
// removed text would be the leak the pass exists to prevent, so the type is
// asserted trivially copyable and there is no place to put a string.
struct RescanTally {
  // How many separate spans were replaced across every string rescanned.
  uint32_t redacted_span_count = 0;
  // How many seeded fixture canaries were present in the input. Non-zero is
  // always a defect upstream of here — either the renderer's redaction did not
  // run, or it ran and missed — and it is counted rather than merely removed
  // so that the failure is visible instead of silently repaired.
  uint32_t canary_hit_count = 0;

  bool anything_was_removed() const { return redacted_span_count > 0; }

  void Add(const RescanTally& other) {
    redacted_span_count += other.redacted_span_count;
    canary_hit_count += other.canary_hit_count;
  }

  friend bool operator==(const RescanTally&, const RescanTally&) = default;
};

static_assert(std::is_trivially_copyable_v<RescanTally>,
              "The rescan tally carries counts only. An owning member would "
              "create a place for the removed text to survive, which is the "
              "opposite of the point.");

// Returns `text` with every secret shape replaced by a static placeholder, and
// adds what was removed to `tally` when one is supplied. Pure and
// deterministic; the scanning is a single forward pass with bounded
// look-ahead, because an unbounded pattern engine on attacker-influenced text
// in the browser process is a cost an attacker would get to choose.
//
// Text longer than ScrubbingSerializer::kMaxScannedCharacters is truncated
// before it is scanned, so what this returns is bounded whatever the renderer
// sent. A name that long is already outside the contract's own bound.
std::string RescanRendererText(std::string_view text, RescanTally* tally);

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_RENDERER_TEXT_RESCAN_H_
