// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_INTERNAL_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_INTERNAL_H_

#include <atomic>
#include <memory>

#include "base/files/file.h"
#include "taffy/services/tool-runtime/media/media_transform.h"

namespace taffy::media_tool {

void RunAudioTransform(bool mono,
                       base::File input,
                       base::File output,
                       TransformLimits limits,
                       std::shared_ptr<std::atomic_bool> cancelled,
                       TransformCallback callback);

void RunFrameTransform(base::File input,
                       base::File output,
                       TransformLimits limits,
                       std::shared_ptr<std::atomic_bool> cancelled,
                       TransformCallback callback);

}  // namespace taffy::media_tool

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TRANSFORM_INTERNAL_H_
