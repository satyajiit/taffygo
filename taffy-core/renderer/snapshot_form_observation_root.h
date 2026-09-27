// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_SNAPSHOT_FORM_OBSERVATION_ROOT_H_
#define TAFFY_RENDERER_SNAPSHOT_FORM_OBSERVATION_ROOT_H_

#include <string_view>

#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy {

class SemanticGraphStore;

// Why an exact form root cannot be used. All non-kOk values fail the whole
// observation before a snapshot is returned; none degrades to a broader read.
enum class FormObservationRootStatus {
  kOk,
  kInvalidShape,
  kAbsentOrStale,
  kWrongDocumentOrType,
};

enum class MediaObservationRootStatus {
  kOk,
  kInvalidShape,
  kAbsentOrStale,
  kWrongDocumentOrType,
};

// Root validation has two distinct jobs. Admission proves that the caller's
// graph precondition is still current before any adapter runs. The final pass
// proves that the exact DOM identity, document, and element type survived the
// collection. Reapplying the caller's revision floor in the final pass would
// reject truthful bounds or visibility annotations made by that same
// collection as if they were page mutations.
enum class ObservationRootValidationPhase {
  kAdmission,
  kAfterCollection,
};

// Validates the SECTION/root shape and then resolves the browser-issued node
// back to one connected HTMLFormElement in this exact document. The admission
// phase applies the caller's minimum revision; the final phase rechecks the
// issued identity without mistaking this request's own annotations for stale
// caller state.
FormObservationRootStatus ValidateFormObservationRoot(
    const mojom::SnapshotRequest& request,
    blink::WebLocalFrame* frame,
    const SemanticGraphStore& store,
    ObservationRootValidationPhase phase);

// Stable bounded diagnostic code; never page-authored text.
std::string_view FormObservationRootDetailCode(
    FormObservationRootStatus status);

// Validates an optional DOCUMENT/media-root shape and resolves it back to one
// connected image or media element in this exact document. This deliberately
// lives beside the form-root validator: both are the renderer's single
// fail-closed gate for browser-issued observation roots.
MediaObservationRootStatus ValidateMediaObservationRoot(
    const mojom::SnapshotRequest& request,
    blink::WebLocalFrame* frame,
    const SemanticGraphStore& store,
    ObservationRootValidationPhase phase);

std::string_view MediaObservationRootDetailCode(
    MediaObservationRootStatus status);

}  // namespace taffy

#endif  // TAFFY_RENDERER_SNAPSHOT_FORM_OBSERVATION_ROOT_H_
