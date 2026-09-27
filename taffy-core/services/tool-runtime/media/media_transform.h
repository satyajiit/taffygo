// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_H_

#include <stdint.h>

#include <atomic>
#include <array>
#include <memory>
#include <string_view>

#include "base/files/file.h"
#include "base/functional/callback_forward.h"
#include "base/time/time.h"
#include "base/types/expected.h"

namespace taffy::media_tool {

// The complete preset surface of the media worker. These are protocol values,
// not command-line fragments: accepting any other string is a refusal.
inline constexpr std::string_view kAudioExtractPreset = "audio.wav.pcm16.v1";
inline constexpr std::string_view kFrameSamplePreset = "frames.png.zip.v1";
inline constexpr std::string_view kTranscodePreset = "audio.wav.mono-pcm16.v1";

// One of the three closed transforms. The value is chosen by the typed job
// discriminator; no worker caller can invent another operation.
enum class TransformKind {
  kExtractAudio,
  kSampleFrames,
  kTranscode,
};

// Bounds applied inside the worker in addition to the browser supervisor's
// process, deadline and byte ceilings.
struct TransformLimits {
  uint64_t max_input_bytes = 0u;
  uint64_t max_output_bytes = 0u;
  uint32_t max_frames = 0u;
  uint32_t max_width = 0u;
  uint32_t max_height = 0u;
  uint32_t max_decoded_buffers = 0u;
  base::TimeDelta max_duration;
  base::TimeTicks deadline;
};

// Content-free terminal reasons. Container and codec controlled text never
// crosses this seam.
enum class TransformFailure {
  kUnreadableInput,
  kUnwritableOutput,
  kInputTooLarge,
  kOutputTooLarge,
  kDurationTooLong,
  kDimensionsTooLarge,
  kTooManyFrames,
  kNoMatchingStream,
  kMalformedMedia,
  kUnsupportedCodec,
  kCancelled,
  kDeadlineExceeded,
};

struct TransformReading {
  uint64_t output_bytes = 0u;
  uint32_t frame_count = 0u;
  std::array<uint8_t, 32u> output_digest{};
};

using TransformResult = base::expected<TransformReading, TransformFailure>;
using TransformCallback = base::OnceCallback<void(TransformResult)>;

// Runs exactly one descriptor-to-descriptor transform. Both descriptors are
// already open and broker-owned. The implementation opens no path or URL and
// receives no codec, container, filter or command-line value.
void TransformMedia(TransformKind kind,
                    base::File input,
                    base::File output,
                    TransformLimits limits,
                    std::shared_ptr<std::atomic_bool> cancelled,
                    TransformCallback callback);

}  // namespace taffy::media_tool

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_H_
