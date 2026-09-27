// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/form_schema_control.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/renderer/adapters/form_control_type_names.h"
#include "taffy/renderer/field_redaction_signals.h"
#include "third_party/blink/public/platform/web_string.h"

namespace taffy::form_schema_internal {
namespace {

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

bool IsChallengeAffordance(const blink::WebElement& element) {
  const std::string tag = base::ToLowerASCII(Utf8(element.TagName()));
  if (tag == "audio") {
    return true;
  }
  const std::string description = base::JoinString(
      {AuthoredAttribute(element, "aria-label"),
       AuthoredAttribute(element, "title"), AuthoredAttribute(element, "alt"),
       AuthoredAttribute(element, "name")},
      " ");
  const std::vector<std::string> tokens =
      field_redaction_signals::TokensAndJoins(description);
  static constexpr auto kAffordances = std::to_array<std::string_view>(
      {"audio", "refresh", "reload", "regenerate", "newimage",
       "newchallenge"});
  return field_redaction_signals::ContainsAnyHint(tokens, kAffordances);
}

// An image that says what it is. The same authored attributes the affordance
// test reads, and one generic word: no vendor, origin or site is named.
bool ImageNamesAChallenge(const blink::WebElement& image) {
  const std::string description = base::JoinString(
      {AuthoredAttribute(image, "aria-label"),
       AuthoredAttribute(image, "title"), AuthoredAttribute(image, "alt")},
      " ");
  static constexpr auto kChallenge =
      std::to_array<std::string_view>({"captcha"});
  return field_redaction_signals::ContainsAnyHint(
      field_redaction_signals::TokensAndJoins(description), kChallenge);
}

bool IsSetTextElement(const blink::WebElement& element) {
  const auto control = element.DynamicTo<blink::WebFormControlElement>();
  return !control.IsNull() &&
         form_control_type_names::IsSetTextControl(
             form_control_type_names::ControlTypeString(
                 control.FormControlTypeForAutofill()));
}

// A hostile page cannot turn challenge detection into an unbounded second
// DOM traversal. Reaching either bound makes the structural proof
// incomplete, so the classifier falls back to ordinary person handover.
constexpr uint32_t kMaxChallengeNodes = 256u;
constexpr uint32_t kMaxChallengeDepth = 16u;

// Walks `root`'s subtree within the bounds above and notes what a challenge
// is made of. `count_text_entries` is false for the form walk, whose count
// comes from the form's own control list, which also holds controls outside
// the element that name the form.
void WalkChallengeStructure(const blink::WebElement& root,
                            bool count_text_entries,
                            ChallengeSignals& signals) {
  std::vector<std::pair<blink::WebNode, uint32_t>> stack;
  // Push in reverse document order because this is a LIFO walk. Besides
  // making the chosen presentation target deterministic, it means "first"
  // below really is the first authored candidate rather than the last sibling.
  for (blink::WebNode child = root.LastChild(); !child.IsNull();
       child = child.PreviousSibling()) {
    stack.emplace_back(child, 1u);
  }
  uint32_t visited = 0;
  while (!stack.empty() && visited < kMaxChallengeNodes) {
    const auto [node, depth] = stack.back();
    stack.pop_back();
    ++visited;
    if (!node.IsElementNode()) {
      continue;
    }
    const blink::WebElement element = node.To<blink::WebElement>();
    const std::string tag = base::ToLowerASCII(Utf8(element.TagName()));
    signals.has_image |= tag == "img";
    signals.has_embedded_widget |=
        tag == "iframe" || tag == "frame" || tag == "object";
    if (tag == "img" && !signals.image_dom_node_id.has_value()) {
      signals.image_dom_node_id = element.GetDomNodeId();
      signals.image_names_a_challenge = ImageNamesAChallenge(element);
    }
    if ((tag == "iframe" || tag == "frame" || tag == "object") &&
        !signals.interactive_dom_node_id.has_value()) {
      signals.interactive_dom_node_id = element.GetDomNodeId();
    }
    signals.has_regenerate_or_audio_affordance |=
        IsChallengeAffordance(element);
    if (count_text_entries && IsSetTextElement(element)) {
      ++signals.text_entry_controls;
    }
    if (depth >= kMaxChallengeDepth) {
      if (!element.FirstChild().IsNull()) {
        signals.structure_complete = false;
      }
      continue;
    }
    for (blink::WebNode child = element.LastChild(); !child.IsNull();
         child = child.PreviousSibling()) {
      stack.emplace_back(child, depth + 1u);
    }
  }
  if (!stack.empty()) {
    signals.structure_complete = false;
  }
}

// The same facts, read from the smallest block around `control` that holds a
// picture and no other field or embedded widget. Empty when no such block is
// within a few wrappers of the field.
ChallengeSignals ChallengeSignalsNear(
    const blink::WebFormControlElement& control) {
  // How far up from the field the block may be. A label, the field, the
  // picture and its buttons sit a few wrappers apart in a component-built
  // form; a block much larger than that is the page, not the challenge.
  constexpr uint32_t kMaxChallengeAncestors = 6u;
  const blink::WebFormElement form = control.Form();
  blink::WebNode node = control.ParentNode();
  for (uint32_t level = 0; level < kMaxChallengeAncestors && !node.IsNull() &&
                           node.IsElementNode();
       ++level, node = node.ParentNode()) {
    const blink::WebElement block = node.To<blink::WebElement>();
    const std::string tag = base::ToLowerASCII(Utf8(block.TagName()));
    // The form is the walk ChallengeSignalsOf already made, and a block past
    // it or past the body holds more than this field's challenge.
    if ((!form.IsNull() && block == form) || tag == "body" || tag == "html") {
      break;
    }
    ChallengeSignals signals;
    WalkChallengeStructure(block, /*count_text_entries=*/true, signals);
    // A block that holds a second field, or that the bounds could not read
    // whole, stops the climb: every larger block holds it too.
    if (!signals.structure_complete || signals.text_entry_controls != 1u) {
      break;
    }
    // A widget block is the interactive kind, which only the form-wide
    // reading may name; this reading adds a picture and nothing else.
    if (signals.has_embedded_widget) {
      break;
    }
    if (signals.has_image) {
      return signals;
    }
  }
  return ChallengeSignals();
}

}  // namespace

ChallengeSignals ChallengeSignalsOf(
    const blink::WebFormElement& form,
    const std::vector<blink::WebFormControlElement>& controls) {
  ChallengeSignals signals;
  constexpr size_t kMaxChallengeControls = 256u;
  const size_t controls_to_read =
      std::min(controls.size(), kMaxChallengeControls);
  for (size_t index = 0; index < controls_to_read; ++index) {
    const blink::WebFormControlElement& control = controls[index];
    if (!control.IsNull() && form_control_type_names::IsSetTextControl(
                                 form_control_type_names::ControlTypeString(
                                     control.FormControlTypeForAutofill()))) {
      ++signals.text_entry_controls;
      // The classifier only distinguishes exactly one from every larger
      // number. Saturating here avoids a page-sized count and lets ordinary
      // multi-field forms stop the control scan early.
      if (signals.text_entry_controls == 2u) {
        break;
      }
    }
  }
  signals.structure_complete = controls.size() <= kMaxChallengeControls;
  if (form.IsNull()) {
    return signals;
  }
  WalkChallengeStructure(form, /*count_text_entries=*/false, signals);
  return signals;
}

FieldChallenge ClassifyFieldChallenge(
    const blink::WebFormControlElement& control,
    const FieldDescriptor& field,
    ProhibitedCategory category,
    const ChallengeSignals& form_signals) {
  const ChallengeKind kind = ClassifyChallenge(field, category, form_signals);
  if (kind == ChallengeKind::kImage) {
    return {kind, form_signals.image_dom_node_id};
  }
  if (kind == ChallengeKind::kInteractive) {
    return {kind, form_signals.interactive_dom_node_id};
  }
  // The block is read only when the form holds a picture somewhere or was too
  // large to read whole, and never for a field already classified: a
  // prohibited field or a one-time code gains nothing from a picture.
  if (kind != ChallengeKind::kNone || category != ProhibitedCategory::kNone ||
      (!form_signals.has_image && form_signals.structure_complete)) {
    return {kind, std::nullopt};
  }
  const ChallengeSignals near = ChallengeSignalsNear(control);
  if (ClassifyChallenge(field, category, near) == ChallengeKind::kImage) {
    return {ChallengeKind::kImage, near.image_dom_node_id};
  }
  return {};
}

}  // namespace taffy::form_schema_internal
