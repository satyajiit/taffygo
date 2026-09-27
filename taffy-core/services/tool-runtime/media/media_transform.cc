// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_transform.h"

#include <utility>

#include "base/functional/callback.h"
#include "taffy/services/tool-runtime/media/media_transform_internal.h"

namespace taffy::media_tool {

void TransformMedia(TransformKind kind,
                    base::File input,
                    base::File output,
                    TransformLimits limits,
                    std::shared_ptr<std::atomic_bool> cancelled,
                    TransformCallback callback) {
  switch (kind) {
    case TransformKind::kExtractAudio:
      RunAudioTransform(/*mono=*/false, std::move(input), std::move(output),
                        limits, std::move(cancelled), std::move(callback));
      return;
    case TransformKind::kSampleFrames:
      RunFrameTransform(std::move(input), std::move(output), limits,
                        std::move(cancelled), std::move(callback));
      return;
    case TransformKind::kTranscode:
      RunAudioTransform(/*mono=*/true, std::move(input), std::move(output),
                        limits, std::move(cancelled), std::move(callback));
      return;
  }
}

}  // namespace taffy::media_tool
