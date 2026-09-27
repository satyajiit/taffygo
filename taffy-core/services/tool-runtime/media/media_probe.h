// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_PROBE_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_PROBE_H_

#include <stdint.h>

#include "base/files/file.h"
#include "base/functional/callback_forward.h"
#include "base/types/expected.h"

namespace taffy::media_tool {

// Everything PROBE_MEDIA is allowed to learn about a file.
//
// Every member is a bounded number, and that is the rule rather than the
// coincidence. A container's strings — title, artist, encoder, codec and
// container names — are written by whoever produced the file, so a probe that
// returned them would make an untrusted party the author of a value the
// browser goes on to display. The contract's MediaProbeResult carries none,
// and this struct is where that decision is kept honest: there is nowhere for
// a string to go.
struct MediaReading {
  uint64_t duration_ms = 0;
  uint32_t audio_streams = 0;
  uint32_t video_streams = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

// Why a probe produced no reading. Closed, and every value maps onto exactly
// one ToolTerminalStatus at the service edge, so a file that could not be
// parsed is never reported as a successful reading of zeroes.
enum class ProbeFailure {
  // The descriptor is not a readable file, or could not be mapped.
  kUnreadableFile,
  // Larger than the budget the job itself declared.
  kInputTooLarge,
  // The demuxer refused it: not a container it understands, or truncated.
  kUnparseableContainer,
  // Parsed, and carries no audio or video stream to report.
  kNoStreams,
};

using ProbeResult = base::expected<MediaReading, ProbeFailure>;
using ProbeCallback = base::OnceCallback<void(ProbeResult)>;

// Reads bounded metadata from one browser-opened file.
//
// `file` is owned for the whole parse and closed with the run. It is the only
// thing this function can reach: there is no path, no name and no second
// descriptor, so a container that references an external file — a playlist, a
// segmented stream, an EXTX manifest — resolves to nothing rather than to
// whatever the sandbox would otherwise have allowed.
//
// Must be called on a sequence that permits blocking, and the reply arrives on
// that same sequence.
void ProbeMedia(base::File file,
                uint64_t max_input_bytes,
                ProbeCallback callback);

}  // namespace taffy::media_tool

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_PROBE_H_
