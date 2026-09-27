// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_CHALLENGE_CLASSIFIER_H_
#define TAFFY_RENDERER_CHALLENGE_CLASSIFIER_H_

#include <stdint.h>

#include <optional>

#include "taffy/renderer/field_redaction.h"

namespace taffy {

// Bounded structural facts about the form around one control. No page value,
// vendor name, origin allowlist, or challenge answer can be represented here.
struct ChallengeSignals {
  bool has_image = false;
  bool has_regenerate_or_audio_affordance = false;
  // Whether the first image's own authored alt, title or label names a
  // CAPTCHA. Evidence of the same kind as the affordance above, read from the
  // picture instead of from a button beside it.
  bool image_names_a_challenge = false;
  bool has_embedded_widget = false;
  // False when either bounded form walk stopped before it could prove the
  // complete shape. A partial shape may never create a challenge hint: the
  // fallback is the ordinary person handover, never a guess from a prefix.
  bool structure_complete = true;
  uint32_t text_entry_controls = 0;
  // DOM identity of the first bounded structural target that supplied the
  // corresponding signal. Renderer-internal only: the browser later asks the
  // renderer to resolve this identity to fresh geometry, and no selector or
  // address leaves the renderer.
  std::optional<int64_t> image_dom_node_id;
  std::optional<int64_t> interactive_dom_node_id;
};

// Classifies only the hint Decision 0088 permits. The result grants nothing:
// policy, capability, dispatch, node re-resolution, and handover remain
// independent gates.
ChallengeKind ClassifyChallenge(const FieldDescriptor& field,
                                ProhibitedCategory prohibited,
                                const ChallengeSignals& signals);

}  // namespace taffy

#endif  // TAFFY_RENDERER_CHALLENGE_CLASSIFIER_H_
