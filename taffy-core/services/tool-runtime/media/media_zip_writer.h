// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_ZIP_WRITER_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_ZIP_WRITER_H_

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"

namespace taffy::media_tool {

// A deterministic, stored-entry ZIP builder for the bounded frame bundle.
// It deliberately exposes only Add and Finish: timestamps, comments,
// compression knobs, paths and filesystem reads do not exist at this seam.
class MediaZipWriter {
 public:
  explicit MediaZipWriter(size_t max_bytes);
  MediaZipWriter(const MediaZipWriter&) = delete;
  MediaZipWriter& operator=(const MediaZipWriter&) = delete;
  ~MediaZipWriter();

  bool Add(std::string name, base::span<const uint8_t> bytes);
  bool Finish(std::vector<uint8_t>* output);

 private:
  struct Entry {
    std::string name;
    uint32_t crc32 = 0u;
    uint32_t size = 0u;
    uint32_t local_offset = 0u;
  };

  bool CanAppend(size_t bytes) const;

  const size_t max_bytes_;
  bool finished_ = false;
  std::vector<Entry> entries_;
  std::vector<uint8_t> bytes_;
};

}  // namespace taffy::media_tool

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_ZIP_WRITER_H_
