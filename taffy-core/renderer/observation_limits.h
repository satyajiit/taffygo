// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_OBSERVATION_LIMITS_H_
#define TAFFY_RENDERER_OBSERVATION_LIMITS_H_

#include <stdint.h>

#include "base/time/time.h"

// The one limits policy for this component.
//
// Protocol section 10 says the numeric node, byte, queue, deadline, and
// update-rate limits are `[Open (OD-031)]` and must be established on the
// supported-device and page corpus. Until that measurement exists the code
// still needs bounds, and the danger is not that the placeholders are wrong -
// they are - but that they get copied. A number that lives in four files gets
// updated in three.
//
// So the shape here is deliberate:
//
//   * Every value lives in observation_limits.json and nowhere else.
//     tools/generate_observation_limits.py turns that file into the generated
//     header this translation unit compiles, and the generator refuses to run
//     when the file's field list and ObservationLimitsValues below disagree.
//     There is no second copy to drift.
//
//   * A call site cannot invent a bound. The grouped limit types below have
//     private constructors and no public aggregate initialisation, and only
//     ObservationLimits can produce one. An adapter that wanted its own
//     ceiling would have to change this header, which is a review, not a
//     patch.
//
//   * A request can narrow and can never widen. NarrowedTo() takes the
//     element-wise stricter of the process-safe ceiling and what the broker
//     asked for. The browser clamps first, for policy; this clamp protects
//     this process. The two answer different questions and neither is
//     redundant.

namespace taffy {

// The generated value block. Field order is part of the contract: the
// generator emits a designated initialiser in exactly this order and fails
// the build if observation_limits.json does not match it name for name.
//
// Every member is a count, a byte count, or a duration in milliseconds. There
// are no floating-point limits on purpose - a bound that a caller has to
// reason about in floating point is a bound nobody checks the edge of.
struct ObservationLimitsValues {
  uint32_t snapshot_max_nodes;
  uint32_t snapshot_max_text_bytes;
  uint32_t snapshot_max_total_bytes;
  uint32_t snapshot_node_framing_bytes;
  uint32_t snapshot_edge_framing_bytes;
  uint32_t snapshot_text_run_framing_bytes;
  uint32_t snapshot_list_element_bytes;
  uint32_t snapshot_max_depth;
  uint32_t snapshot_deadline_ms;
  uint32_t snapshot_max_frames;

  uint32_t delta_max_queued_signals;
  uint32_t delta_max_queued_bytes;
  uint32_t delta_coalescing_window_ms;
  uint32_t delta_min_interval_ms;
  uint32_t delta_max_coalescing_window_ms;
  uint32_t delta_estimated_signal_bytes;
  uint32_t delta_max_tracked_cancellations;

  uint32_t structured_data_max_block_bytes;
  uint32_t structured_data_max_json_depth;
  uint32_t structured_data_max_blocks;
  uint32_t structured_data_max_values_per_property;

  uint32_t forms_max_forms;
  uint32_t forms_max_controls_per_form;
  uint32_t forms_max_formless_controls;

  uint32_t selection_max_text_bytes;
  uint32_t selection_max_nodes;

  uint32_t layout_max_occlusion_probes;
  uint32_t layout_min_visible_area_px;
  uint32_t layout_viewport_margin_px;

  uint32_t content_signal_max_text_scan_bytes;
  uint32_t content_signal_min_encoded_blob_chars;

  uint32_t redaction_min_digit_run;
  uint32_t redaction_max_digit_separators;
  uint32_t redaction_min_hex_run;
  uint32_t redaction_min_token_run;
  uint32_t redaction_min_seed_phrase_words;
  uint32_t redaction_max_text_scan_bytes;
};

class ObservationLimits;

// What one extraction may spend. This is the group a request is allowed to
// narrow; everything else is a property of the process.
class SnapshotBudget {
 public:
  uint32_t max_nodes() const { return max_nodes_; }
  uint32_t max_text_bytes() const { return max_text_bytes_; }
  uint32_t max_total_bytes() const { return max_total_bytes_; }
  // What a node, an edge, a text run and one closed-vocabulary list element
  // cost in framing before their own strings. These are terms in the estimate
  // that is charged against `max_total_bytes`, not bounds of their own, and
  // `NarrowedTo` deliberately leaves them alone: a request cannot ask for a
  // different encoding. They exist because charging `sizeof(SemanticNode)`
  // made this process spend a budget the browser measures in encoded bytes.
  uint32_t node_framing_bytes() const { return node_framing_bytes_; }
  uint32_t edge_framing_bytes() const { return edge_framing_bytes_; }
  uint32_t text_run_framing_bytes() const { return text_run_framing_bytes_; }
  uint32_t list_element_bytes() const { return list_element_bytes_; }
  uint32_t max_depth() const { return max_depth_; }
  uint32_t max_frames() const { return max_frames_; }
  base::TimeDelta deadline() const { return base::Milliseconds(deadline_ms_); }
  uint32_t deadline_ms() const { return deadline_ms_; }

  // Out of line, and public because a caller may hold a copy of the budget it
  // was given. With the four framing terms this class has ten initialized
  // members, which is one past what the chromium-style plugin will inline.
  SnapshotBudget(const SnapshotBudget&);
  SnapshotBudget& operator=(const SnapshotBudget&);
  ~SnapshotBudget();

 private:
  friend class ObservationLimits;
  SnapshotBudget();

  uint32_t max_nodes_ = 0;
  uint32_t max_text_bytes_ = 0;
  uint32_t max_total_bytes_ = 0;
  uint32_t node_framing_bytes_ = 0;
  uint32_t edge_framing_bytes_ = 0;
  uint32_t text_run_framing_bytes_ = 0;
  uint32_t list_element_bytes_ = 0;
  uint32_t max_depth_ = 0;
  uint32_t max_frames_ = 0;
  uint32_t deadline_ms_ = 0;
};

class DeltaLimits {
 public:
  uint32_t max_queued_signals() const { return max_queued_signals_; }
  uint32_t max_queued_bytes() const { return max_queued_bytes_; }
  base::TimeDelta coalescing_window() const {
    return base::Milliseconds(coalescing_window_ms_);
  }
  uint32_t min_interval_ms() const { return min_interval_ms_; }
  base::TimeDelta min_interval() const {
    return base::Milliseconds(min_interval_ms_);
  }
  uint32_t estimated_signal_bytes() const { return estimated_signal_bytes_; }
  uint32_t max_tracked_cancellations() const {
    return max_tracked_cancellations_;
  }

  // Clamps a broker-supplied coalescing window into the range this process
  // will honour. The broker has already clamped it; clamping again keeps a
  // broker defect from becoming a renderer stall.
  base::TimeDelta ClampCoalescingWindow(base::TimeDelta requested) const;

 private:
  friend class ObservationLimits;
  DeltaLimits() = default;

  uint32_t max_queued_signals_ = 0;
  uint32_t max_queued_bytes_ = 0;
  uint32_t coalescing_window_ms_ = 0;
  uint32_t min_interval_ms_ = 0;
  uint32_t max_coalescing_window_ms_ = 0;
  uint32_t estimated_signal_bytes_ = 0;
  uint32_t max_tracked_cancellations_ = 0;
};

class StructuredDataLimits {
 public:
  uint32_t max_block_bytes() const { return max_block_bytes_; }
  int max_json_depth() const { return static_cast<int>(max_json_depth_); }
  uint32_t max_blocks() const { return max_blocks_; }
  uint32_t max_values_per_property() const { return max_values_per_property_; }

 private:
  friend class ObservationLimits;
  StructuredDataLimits() = default;

  uint32_t max_block_bytes_ = 0;
  uint32_t max_json_depth_ = 0;
  uint32_t max_blocks_ = 0;
  uint32_t max_values_per_property_ = 0;
};

class FormLimits {
 public:
  uint32_t max_forms() const { return max_forms_; }
  uint32_t max_controls_per_form() const { return max_controls_per_form_; }
  uint32_t max_formless_controls() const { return max_formless_controls_; }

 private:
  friend class ObservationLimits;
  FormLimits() = default;

  uint32_t max_forms_ = 0;
  uint32_t max_controls_per_form_ = 0;
  uint32_t max_formless_controls_ = 0;
};

class SelectionLimits {
 public:
  uint32_t max_text_bytes() const { return max_text_bytes_; }
  uint32_t max_nodes() const { return max_nodes_; }

 private:
  friend class ObservationLimits;
  SelectionLimits() = default;

  uint32_t max_text_bytes_ = 0;
  uint32_t max_nodes_ = 0;
};

class LayoutLimits {
 public:
  uint32_t max_occlusion_probes() const { return max_occlusion_probes_; }
  uint32_t min_visible_area_px() const { return min_visible_area_px_; }
  uint32_t viewport_margin_px() const { return viewport_margin_px_; }

 private:
  friend class ObservationLimits;
  LayoutLimits() = default;

  uint32_t max_occlusion_probes_ = 0;
  uint32_t min_visible_area_px_ = 0;
  uint32_t viewport_margin_px_ = 0;
};

// Work and shape bounds for content signals. Detection is evidence, not an
// authorization boundary, but an attacker-authored string still must not buy
// an unbounded scan in the renderer.
class ContentSignalLimits {
 public:
  uint32_t max_text_scan_bytes() const { return max_text_scan_bytes_; }
  uint32_t min_encoded_blob_chars() const { return min_encoded_blob_chars_; }

 private:
  friend class ObservationLimits;
  ContentSignalLimits() = default;

  uint32_t max_text_scan_bytes_ = 0;
  uint32_t min_encoded_blob_chars_ = 0;
};

class RedactionLimits {
 public:
  uint32_t min_digit_run() const { return min_digit_run_; }
  uint32_t max_digit_separators() const { return max_digit_separators_; }
  uint32_t min_hex_run() const { return min_hex_run_; }
  uint32_t min_token_run() const { return min_token_run_; }
  uint32_t min_seed_phrase_words() const { return min_seed_phrase_words_; }
  uint32_t max_text_scan_bytes() const { return max_text_scan_bytes_; }

 private:
  friend class ObservationLimits;
  RedactionLimits() = default;

  uint32_t min_digit_run_ = 0;
  uint32_t max_digit_separators_ = 0;
  uint32_t min_hex_run_ = 0;
  uint32_t min_token_run_ = 0;
  uint32_t min_seed_phrase_words_ = 0;
  uint32_t max_text_scan_bytes_ = 0;
};

// What a broker asked one extraction to cost. Every member is a request, not
// a grant: NarrowedTo() takes the stricter of this and the ceiling, and a
// zero member means "the broker stated no preference" rather than "zero".
struct RequestedSnapshotBudget {
  uint32_t max_nodes = 0;
  uint32_t max_text_bytes = 0;
  uint32_t max_total_bytes = 0;
  uint32_t max_depth = 0;
  uint32_t deadline_ms = 0;
};

class ObservationLimits {
 public:
  // The compiled-in ceiling, built from observation_limits.json. Returned by
  // reference so that the identity of the policy object is stable and an
  // adapter that held a reference cannot be handed a different one.
  static const ObservationLimits& ProcessSafeCeiling();

  // For tests that need a deliberately tiny budget to exercise a truncation
  // path. It can only ever produce something stricter than the ceiling, so a
  // test cannot use it to widen a bound.
  static ObservationLimits NarrowedForTesting(
      const RequestedSnapshotBudget& requested);

  ObservationLimits(const ObservationLimits&);
  ObservationLimits& operator=(const ObservationLimits&);
  ~ObservationLimits();

  // The element-wise stricter of this policy and `requested`. A zero member
  // in `requested` is treated as "no preference stated" and leaves the
  // ceiling in place - not as a request for zero, which would silently make
  // every extraction empty.
  ObservationLimits NarrowedTo(const RequestedSnapshotBudget& requested) const;

  const SnapshotBudget& snapshot() const { return snapshot_; }
  const DeltaLimits& delta() const { return delta_; }
  const StructuredDataLimits& structured_data() const {
    return structured_data_;
  }
  const FormLimits& forms() const { return forms_; }
  const SelectionLimits& selection() const { return selection_; }
  const LayoutLimits& layout() const { return layout_; }
  const ContentSignalLimits& content_signals() const {
    return content_signals_;
  }
  const RedactionLimits& redaction() const { return redaction_; }

 private:
  explicit ObservationLimits(const ObservationLimitsValues& values);

  SnapshotBudget snapshot_;
  DeltaLimits delta_;
  StructuredDataLimits structured_data_;
  FormLimits forms_;
  SelectionLimits selection_;
  LayoutLimits layout_;
  ContentSignalLimits content_signals_;
  RedactionLimits redaction_;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_OBSERVATION_LIMITS_H_
