// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_SINK_H_
#define TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_SINK_H_

#include <stdint.h>

#include <array>
#include <optional>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"

namespace taffy {

// The half of a transfer that touches the disk. Lives on a blocking sequence.
//
// It exists as its own class rather than as methods on `AssetTransfer` so that
// the sequence boundary is a type boundary: nothing here can be called from the
// browser's UI thread, because nothing here is reachable from it except through
// `base::SequenceBound`.
class AssetTransferSink {
 public:
  explicit AssetTransferSink(base::FilePath store_root);
  AssetTransferSink(const AssetTransferSink&) = delete;
  AssetTransferSink& operator=(const AssetTransferSink&) = delete;
  ~AssetTransferSink();

  // Opens the staging file at `resume_from` and hashes the prefix already
  // there. False means nothing was opened and the transfer cannot proceed.
  bool Open(std::string asset_id,
            std::string asset_revision,
            uint64_t resume_from);

  // Appends one chunk and hashes it. Answers the total bytes on disk, or
  // nothing when the write failed — which ends the transfer rather than
  // silently producing a file with a gap in it.
  std::optional<uint64_t> Write(std::string chunk);

  // Finishes the hash. The sink cannot be written to afterwards.
  std::array<uint8_t, 32> Finish();

  // Moves the staged file to its installed place.
  bool Commit();

 private:
  AssetStore store_;
  std::string asset_id_;
  std::string asset_revision_;
  base::File file_;
  crypto::hash::Hasher hasher_{crypto::hash::kSha256};
  uint64_t written_bytes_ = 0;
  bool finished_ = false;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_SINK_H_
