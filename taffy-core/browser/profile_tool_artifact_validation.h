// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_VALIDATION_H_
#define TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_VALIDATION_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace taffy {

struct VerifiedToolOutput {
  core_service::mojom::TaskArtifactKind kind;
  std::vector<uint8_t> digest;
  std::vector<uint8_t> content;
};

// Validates the exact fixed builders shipped in the bundled Python runtime.
// The ZIP is complete (not merely prefix-shaped) and has no trailing bytes.
std::optional<VerifiedToolOutput> ValidateBundledPythonOutput(
    std::string entrypoint,
    std::vector<uint8_t> content);

// Validates the exact descriptor output produced by the media worker and an
// independently computed digest. `frame_count` is used only for an archive.
std::optional<VerifiedToolOutput> ValidateMediaOutput(
    core_service::mojom::TaskArtifactKind kind,
    uint32_t frame_count,
    std::vector<uint8_t> expected_digest,
    std::vector<uint8_t> content);

// A probe produces no file. This canonical fixed-width receipt lets the task
// journal prove which bounded reading completed without retaining model text.
VerifiedToolOutput EncodeMediaProbe(
    const core_service::mojom::MediaProbeResult& result);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_VALIDATION_H_
