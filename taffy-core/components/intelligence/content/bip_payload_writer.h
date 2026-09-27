// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_PAYLOAD_WRITER_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_PAYLOAD_WRITER_H_

#include <stdint.h>

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// The bounded byte writer the BIP graph framing is built out of, and nothing
// else. It knows how to put a number or a string into a buffer and how to stop;
// it knows nothing about nodes, sensitivity, or what any of the bytes mean.
//
// It is told its ceiling rather than reading a constant, so that it depends on
// no part of the framing it serves. A writer that had to know which payload it
// was writing would be a second place the framing is described.

namespace taffy::bip_payload {

// The writer refuses rather than truncating. A refusal is one branch the
// caller already has to handle; a truncation is a payload the isolated core
// would decode successfully and read wrongly.
class Writer {
 public:
  explicit Writer(size_t ceiling);
  ~Writer();

  bool ok() const { return ok_; }

  // True only when a write was refused because it would not fit under the
  // ceiling. `ok()` is false for that and also for a field whose declared size
  // is implausible, and the two are different answers to give a caller: one
  // means "ask for less", the other means "this input is not well formed".
  // Nothing here decides which; it only stops the encoder having to guess.
  bool overflowed() const { return overflowed_; }

  void U8(uint8_t value) {
    if (!CanAppend(1u)) {
      return;
    }
    bytes_.push_back(value);
  }

  void U16(uint16_t value) {
    if (!CanAppend(sizeof(value))) {
      return;
    }
    bytes_.push_back(static_cast<uint8_t>(value & 0xFFu));
    bytes_.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
  }

  void U32(uint32_t value) {
    if (!CanAppend(sizeof(value))) {
      return;
    }
    for (size_t shift = 0; shift < 32u; shift += 8u) {
      bytes_.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
    }
  }

  void U64(uint64_t value) {
    if (!CanAppend(sizeof(value))) {
      return;
    }
    for (size_t shift = 0; shift < 64u; shift += 8u) {
      bytes_.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
    }
  }

  // A short string: identifiers, origins, names. The length prefix is 16 bits
  // because everything written through here is bounded by the protocol at far
  // less than that, and a value that is not is a defect worth failing on.
  void Short(const std::string& value) {
    if (value.size() > std::numeric_limits<uint16_t>::max()) {
      ok_ = false;
      return;
    }
    const size_t write_size = sizeof(uint16_t) + value.size();
    if (!CanAppend(write_size)) {
      return;
    }
    const uint16_t size = static_cast<uint16_t>(value.size());
    bytes_.push_back(static_cast<uint8_t>(size & 0xFFu));
    bytes_.push_back(static_cast<uint8_t>((size >> 8) & 0xFFu));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  void Blob(const std::vector<uint8_t>& value) {
    if (value.size() > std::numeric_limits<uint32_t>::max() ||
        value.size() > std::numeric_limits<size_t>::max() - sizeof(uint32_t)) {
      ok_ = false;
      return;
    }
    const size_t write_size = sizeof(uint32_t) + value.size();
    if (!CanAppend(write_size)) {
      return;
    }
    const uint32_t size = static_cast<uint32_t>(value.size());
    for (size_t shift = 0; shift < 32u; shift += 8u) {
      bytes_.push_back(static_cast<uint8_t>((size >> shift) & 0xFFu));
    }
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  // Element counts are written as u32 and every one of them is a container
  // size, so this is the one place that refuses an implausible count.
  void Count(size_t value) {
    if (!ok_) {
      return;
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
      ok_ = false;
      return;
    }
    U32(static_cast<uint32_t>(value));
  }

  std::vector<uint8_t> Take() {
    if (!ok_) {
      return {};
    }
    return std::move(bytes_);
  }

 private:
  // Checks the complete write before appending any byte. Besides avoiding a
  // branch per byte for strings and blobs, this leaves no partial field in the
  // scratch buffer after a refusal and makes the ceiling an allocation bound,
  // not merely a property checked after allocation.
  bool CanAppend(size_t byte_count) {
    if (!ok_) {
      return false;
    }
    if (bytes_.size() > ceiling_ || byte_count > ceiling_ - bytes_.size()) {
      ok_ = false;
      overflowed_ = true;
      return false;
    }
    return true;
  }

  const size_t ceiling_;
  std::vector<uint8_t> bytes_;
  bool ok_ = true;
  bool overflowed_ = false;
};

}  // namespace taffy::bip_payload

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_BIP_PAYLOAD_WRITER_H_
