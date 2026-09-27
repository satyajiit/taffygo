// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/challenge_classifier.h"

namespace taffy {
namespace {

bool IsTextEntry(std::string_view type) {
  return type == "text" || type == "search" || type == "email" ||
         type == "tel" || type == "url" || type == "number" ||
         type == "textarea";
}

}  // namespace

ChallengeKind ClassifyChallenge(const FieldDescriptor& field,
                                ProhibitedCategory prohibited,
                                const ChallengeSignals& signals) {
  if (prohibited == ProhibitedCategory::kOneTimeCode) {
    return ChallengeKind::kOneTimeCode;
  }
  // A password, recovery code, card security code, key, or token never gains a
  // friendlier classification from nearby page-authored structure.
  if (prohibited != ProhibitedCategory::kNone ||
      !IsTextEntry(field.control_type) || !signals.structure_complete) {
    return ChallengeKind::kNone;
  }
  if (signals.has_embedded_widget && signals.text_entry_controls == 1u) {
    return ChallengeKind::kInteractive;
  }
  if (signals.has_image &&
      (signals.has_regenerate_or_audio_affordance ||
       signals.image_names_a_challenge) &&
      signals.text_entry_controls == 1u) {
    return ChallengeKind::kImage;
  }
  return ChallengeKind::kNone;
}

}  // namespace taffy
