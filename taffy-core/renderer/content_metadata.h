// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_CONTENT_METADATA_H_
#define TAFFY_RENDERER_CONTENT_METADATA_H_

#include <array>
#include <string_view>

#include "taffy/renderer/observation_limits.h"
#include "taffy/renderer/semantic_graph.h"

// Content authorship and injection-shape evidence computed while an adapter
// already holds one bounded piece of page text (protocol section 9.4).
//
// This module deliberately has no Blink dependency. Adapters supply the few
// presentation facts only they can know -- hidden by computed style, an
// authored language mismatch, and the browser's cross-origin-frame fact --
// and this module does the bounded byte inspection. It never grants authority,
// changes sensitivity, or copies text.

namespace taffy::content_metadata {

// The only order signals may cross in. Keeping it beside the detector lets
// the renderer use a cheap mask internally while the serializer emits a
// sorted, duplicate-free closed-enumeration array.
inline constexpr std::array<ContentSignal, 7> kCanonicalSignals = {
    ContentSignal::kHiddenByStyle,
    ContentSignal::kZeroWidthCharacters,
    ContentSignal::kBidiControlCharacters,
    ContentSignal::kEncodedBlob,
    ContentSignal::kImperativeInstructionShape,
    ContentSignal::kLanguageMismatch,
    ContentSignal::kCrossOriginFrameAuthored,
};

// Initial authorship from a fact the browser supplied. A cross-origin frame
// cannot become first party because page markup said so.
RendererContentTrust DocumentTrust(bool browser_reported_cross_origin);

// Same, narrowed for a page region explicitly marked as reader-authored.
// Cross-origin remains the stricter label.
RendererContentTrust UserGeneratedTrust(bool browser_reported_cross_origin);

// The conservative single-label join used for snapshot summaries.
RendererContentTrust LeastTrusted(RendererContentTrust left,
                                  RendererContentTrust right);

// Presentation findings already established by the adapter. These are ORed
// with text-shape findings and never interpreted as authorization.
ContentSignalMask ContextSignals(bool hidden_by_style,
                                 bool language_mismatch,
                                 bool browser_reported_cross_origin);

// Positive authored-language disagreement only. Empty or equal primary
// subtags mean unknown/no finding; absence is never treated as agreement.
bool AuthoredLanguagesDiffer(std::string_view document_language,
                             std::string_view content_language);

// Bounded, allocation-free text-shape inspection.
ContentSignalMask DetectTextSignals(std::string_view text,
                                    const ContentSignalLimits& limits);

// Applies the initial node label and presentation signals.
void ApplyNodeContext(SemanticNode* node,
                      RendererContentTrust trust,
                      ContentSignalMask context_signals);

// Adds findings for a node-level string such as name or description.
void AddNodeTextSignals(SemanticNode* node,
                        std::string_view text,
                        ContentSignalMask context_signals,
                        const ContentSignalLimits& limits);

// Labels one run and joins all of its findings onto the containing node.
void LabelTextRun(TextRun* run,
                  SemanticNode* node,
                  RendererContentTrust trust,
                  ContentSignalMask context_signals,
                  const ContentSignalLimits& limits);

constexpr bool HasSignal(ContentSignalMask mask, ContentSignal signal) {
  return (mask & ContentSignalBit(signal)) != 0;
}

constexpr ContentSignalMask KnownSignalMask() {
  ContentSignalMask mask = 0;
  for (ContentSignal signal : kCanonicalSignals) {
    mask |= ContentSignalBit(signal);
  }
  return mask;
}

}  // namespace taffy::content_metadata

#endif  // TAFFY_RENDERER_CONTENT_METADATA_H_
