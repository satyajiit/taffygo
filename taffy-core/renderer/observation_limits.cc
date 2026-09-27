// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/observation_limits.h"

#include <algorithm>

#include "base/no_destructor.h"

// The generated value block. It is produced at build time from
// observation_limits.json by tools/generate_observation_limits.py and is not
// committed: committing it would create the second copy this whole design
// exists to prevent.
//
// VERIFY AT SP-04: that $target_gen_dir is on the include path for this
// target under the overlay mount, so this include resolves the same way a
// generated mojom header does. If it does not, the action's output directory
// is the thing to fix - not this include, and not by committing the header.
#include "taffy/renderer/observation_limits_generated.h"

namespace taffy {

namespace {

// "Stricter" for a bound means smaller, with one exception that matters: a
// requested value of zero is a broker that stated no preference, not a broker
// asking for an empty extraction. Treating it as zero would make a defaulted
// request silently produce nothing, and an empty snapshot that looks
// successful is the failure shape protocol section 15 is written against.
uint32_t Narrow(uint32_t ceiling, uint32_t requested) {
  return requested == 0 ? ceiling : std::min(ceiling, requested);
}

}  // namespace

base::TimeDelta DeltaLimits::ClampCoalescingWindow(
    base::TimeDelta requested) const {
  return std::clamp(requested, base::Milliseconds(min_interval_ms_),
                    base::Milliseconds(max_coalescing_window_ms_));
}

SnapshotBudget::SnapshotBudget() = default;
SnapshotBudget::SnapshotBudget(const SnapshotBudget&) = default;
SnapshotBudget& SnapshotBudget::operator=(const SnapshotBudget&) = default;
SnapshotBudget::~SnapshotBudget() = default;

ObservationLimits::ObservationLimits(const ObservationLimitsValues& values) {
  snapshot_.max_nodes_ = values.snapshot_max_nodes;
  snapshot_.max_text_bytes_ = values.snapshot_max_text_bytes;
  snapshot_.max_total_bytes_ = values.snapshot_max_total_bytes;
  snapshot_.node_framing_bytes_ = values.snapshot_node_framing_bytes;
  snapshot_.edge_framing_bytes_ = values.snapshot_edge_framing_bytes;
  snapshot_.text_run_framing_bytes_ = values.snapshot_text_run_framing_bytes;
  snapshot_.list_element_bytes_ = values.snapshot_list_element_bytes;
  snapshot_.max_depth_ = values.snapshot_max_depth;
  snapshot_.max_frames_ = values.snapshot_max_frames;
  snapshot_.deadline_ms_ = values.snapshot_deadline_ms;

  delta_.max_queued_signals_ = values.delta_max_queued_signals;
  delta_.max_queued_bytes_ = values.delta_max_queued_bytes;
  delta_.coalescing_window_ms_ = values.delta_coalescing_window_ms;
  delta_.min_interval_ms_ = values.delta_min_interval_ms;
  delta_.max_coalescing_window_ms_ = values.delta_max_coalescing_window_ms;
  delta_.estimated_signal_bytes_ = values.delta_estimated_signal_bytes;
  delta_.max_tracked_cancellations_ = values.delta_max_tracked_cancellations;

  structured_data_.max_block_bytes_ = values.structured_data_max_block_bytes;
  structured_data_.max_json_depth_ = values.structured_data_max_json_depth;
  structured_data_.max_blocks_ = values.structured_data_max_blocks;
  structured_data_.max_values_per_property_ =
      values.structured_data_max_values_per_property;

  forms_.max_forms_ = values.forms_max_forms;
  forms_.max_controls_per_form_ = values.forms_max_controls_per_form;
  forms_.max_formless_controls_ = values.forms_max_formless_controls;

  selection_.max_text_bytes_ = values.selection_max_text_bytes;
  selection_.max_nodes_ = values.selection_max_nodes;

  layout_.max_occlusion_probes_ = values.layout_max_occlusion_probes;
  layout_.min_visible_area_px_ = values.layout_min_visible_area_px;
  layout_.viewport_margin_px_ = values.layout_viewport_margin_px;

  content_signals_.max_text_scan_bytes_ =
      values.content_signal_max_text_scan_bytes;
  content_signals_.min_encoded_blob_chars_ =
      values.content_signal_min_encoded_blob_chars;

  redaction_.min_digit_run_ = values.redaction_min_digit_run;
  redaction_.max_digit_separators_ = values.redaction_max_digit_separators;
  redaction_.min_hex_run_ = values.redaction_min_hex_run;
  redaction_.min_token_run_ = values.redaction_min_token_run;
  redaction_.min_seed_phrase_words_ = values.redaction_min_seed_phrase_words;
  redaction_.max_text_scan_bytes_ = values.redaction_max_text_scan_bytes;
}

ObservationLimits::ObservationLimits(const ObservationLimits&) = default;
ObservationLimits& ObservationLimits::operator=(const ObservationLimits&) =
    default;
ObservationLimits::~ObservationLimits() = default;

// static
const ObservationLimits& ObservationLimits::ProcessSafeCeiling() {
  // The inner temporary is brace-initialised, not parenthesised: with
  // parentheses the whole declaration is a most-vexing-parse and clang reads
  // `ceiling` as a function taking one ObservationLimits. NoDestructor cannot
  // construct the ObservationLimits in place instead, because the
  // ObservationLimitsValues constructor is private and NoDestructor is not a
  // friend - so the ceiling is built here, where the private constructor is
  // reachable, and copied in.
  static const base::NoDestructor<ObservationLimits> ceiling(
      ObservationLimits{generated::kProcessSafeCeilingValues});
  return *ceiling;
}

// static
ObservationLimits ObservationLimits::NarrowedForTesting(
    const RequestedSnapshotBudget& requested) {
  return ProcessSafeCeiling().NarrowedTo(requested);
}

ObservationLimits ObservationLimits::NarrowedTo(
    const RequestedSnapshotBudget& requested) const {
  ObservationLimits narrowed(*this);
  narrowed.snapshot_.max_nodes_ =
      Narrow(snapshot_.max_nodes_, requested.max_nodes);
  narrowed.snapshot_.max_text_bytes_ =
      Narrow(snapshot_.max_text_bytes_, requested.max_text_bytes);
  narrowed.snapshot_.max_total_bytes_ =
      Narrow(snapshot_.max_total_bytes_, requested.max_total_bytes);
  narrowed.snapshot_.max_depth_ =
      Narrow(snapshot_.max_depth_, requested.max_depth);
  narrowed.snapshot_.deadline_ms_ =
      Narrow(snapshot_.deadline_ms_, requested.deadline_ms);
  // Frames, delta, structured-data, form, selection, layout, content-signal,
  // and redaction bounds are properties of this process, not of a request. A
  // broker cannot ask for a deeper JSON parse or a longer pattern scan, so
  // there is nothing here to narrow and nothing to widen.
  return narrowed;
}

}  // namespace taffy
