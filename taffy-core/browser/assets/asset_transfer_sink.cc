// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_transfer_sink.h"

#include <limits>
#include <utility>

#include "base/containers/span.h"
#include "base/numerics/safe_conversions.h"

namespace taffy {
namespace {

// How much of a staged prefix is read at a time when a transfer resumes.
// A quarter of a megabyte: large enough that a twelve-megabyte prefix is fifty
// reads, small enough that the buffer is not worth thinking about.
constexpr size_t kPrefixChunkBytes = 256u * 1024u;

}  // namespace

AssetTransferSink::AssetTransferSink(base::FilePath store_root)
    : store_(std::move(store_root)) {}

AssetTransferSink::~AssetTransferSink() = default;

bool AssetTransferSink::Open(std::string asset_id,
                             std::string asset_revision,
                             uint64_t resume_from) {
  asset_id_ = std::move(asset_id);
  asset_revision_ = std::move(asset_revision);
  file_ = store_.OpenStaging(asset_id_, asset_revision_, resume_from);
  if (!file_.IsValid()) {
    return false;
  }
  written_bytes_ = resume_from;

  // Hash what is already there. `OpenStaging` truncated the file to
  // `resume_from`, so this reads exactly the bytes a later digest must cover.
  std::string buffer(kPrefixChunkBytes, '\0');
  uint64_t offset = 0;
  while (offset < resume_from) {
    const uint64_t remaining = resume_from - offset;
    const size_t wanted = static_cast<size_t>(
        std::min<uint64_t>(remaining, kPrefixChunkBytes));
    const std::optional<size_t> read = file_.Read(
        base::checked_cast<int64_t>(offset),
        base::as_writable_byte_span(buffer).first(wanted));
    if (!read.has_value() || *read == 0) {
      // The file is shorter than it said it was. Nothing is salvageable, so
      // the sink refuses rather than hashing a prefix that is not the prefix.
      file_ = base::File();
      return false;
    }
    hasher_.Update(base::as_byte_span(buffer).first(*read));
    offset += *read;
  }

  // `Seek` answers the new offset, and a seek to the start of a fresh file
  // answers zero. Comparing against the offset asked for rather than testing
  // the answer for truth is what keeps a correct seek to byte zero from
  // reading as a failure.
  const int64_t wanted_offset = base::checked_cast<int64_t>(resume_from);
  if (file_.Seek(base::File::FROM_BEGIN, wanted_offset) != wanted_offset) {
    file_ = base::File();
    return false;
  }
  return true;
}

std::optional<uint64_t> AssetTransferSink::Write(std::string chunk) {
  if (!file_.IsValid() || finished_) {
    return std::nullopt;
  }
  if (chunk.empty()) {
    return written_bytes_;
  }
  if (chunk.size() >
      std::numeric_limits<uint64_t>::max() - written_bytes_) {
    return std::nullopt;
  }
  const std::optional<size_t> written =
      file_.WriteAtCurrentPos(base::as_byte_span(chunk));
  if (!written.has_value() || *written != chunk.size()) {
    return std::nullopt;
  }
  hasher_.Update(base::as_byte_span(chunk));
  written_bytes_ += *written;
  return written_bytes_;
}

std::array<uint8_t, 32> AssetTransferSink::Finish() {
  std::array<uint8_t, 32> digest = {};
  if (finished_) {
    return digest;
  }
  finished_ = true;
  hasher_.Finish(digest);
  // Flush before anything judges the bytes. A digest that matched a buffer the
  // operating system had not written yet would be a verdict about memory.
  if (file_.IsValid()) {
    file_.Flush();
  }
  return digest;
}

bool AssetTransferSink::Commit() {
  // Close first. A rename over an open handle succeeds on POSIX and does not
  // on Windows, and the plane must behave the same on both.
  file_ = base::File();
  return store_.Commit(asset_id_, asset_revision_);
}

}  // namespace taffy
