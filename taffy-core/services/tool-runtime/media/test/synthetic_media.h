// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_TEST_SYNTHETIC_MEDIA_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_TEST_SYNTHETIC_MEDIA_H_

#include <stdint.h>

#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"

namespace taffy::media_tool::test {

// A RIFF/WAVE container holding `duration_ms` of 8 kHz mono 16-bit silence.
//
// The suite builds its own container rather than checking one in. A committed
// media file would be a binary under a source tree that the provenance sweep
// would then have to account for, and its duration and stream count would be
// facts a reader has to take on trust; here they are arguments beside the
// assertion about them.
std::vector<uint8_t> BuildSilentWave(uint32_t duration_ms);

// The same container with one or two channels. The overload exists for the
// extraction/transcode tests: extraction must preserve a stereo source while
// the reviewed mono preset must not.
std::vector<uint8_t> BuildSilentWave(uint32_t duration_ms, uint16_t channels);

// Writes `bytes` into `directory` and reopens it read-only, which is the shape
// the browser's handle broker hands a worker.
base::File WriteReadOnlyFile(const base::FilePath& directory,
                             base::span<const uint8_t> bytes);

}  // namespace taffy::media_tool::test

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_TEST_SYNTHETIC_MEDIA_H_
