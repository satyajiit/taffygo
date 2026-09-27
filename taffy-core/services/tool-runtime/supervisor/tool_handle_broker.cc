// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"

#include <stdint.h>

#include <utility>
#include <vector>

#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"

namespace taffy {
namespace {

// Sixteen bytes, hex-encoded, is thirty-two characters: comfortably inside the
// contract's handle bound and far past anything an attacker enumerates.
constexpr size_t kHandleEntropyBytes = 16u;

}  // namespace

ToolHandleBroker::ToolHandleBroker() = default;
ToolHandleBroker::~ToolHandleBroker() = default;

void ToolHandleBroker::SetResolvePort(ResolvePort resolve) {
  resolve_ = std::move(resolve);
}

std::string ToolHandleBroker::Mint(const std::string& job_id, Mode mode) {
  if (job_id.empty()) {
    return std::string();
  }
  uint8_t entropy[kHandleEntropyBytes];
  base::RandBytes(entropy);
  std::string handle_id = base::HexEncodeLower(entropy);
  records_.insert_or_assign(handle_id, Record{job_id, mode});
  return handle_id;
}

bool ToolHandleBroker::IsMintedFor(const std::string& job_id,
                                   const std::string& handle_id,
                                   Mode mode) const {
  const auto found = records_.find(handle_id);
  return found != records_.end() && found->second.job_id == job_id &&
         found->second.mode == mode;
}

base::File ToolHandleBroker::Resolve(const std::string& job_id,
                                     const std::string& handle_id,
                                     Mode mode) {
  if (resolve_.is_null() || !IsMintedFor(job_id, handle_id, mode)) {
    return base::File();
  }
  return resolve_.Run(handle_id, mode);
}

void ToolHandleBroker::RevokeJob(const std::string& job_id) {
  std::vector<std::string> revoked;
  for (const auto& entry : records_) {
    if (entry.second.job_id == job_id) {
      revoked.push_back(entry.first);
    }
  }
  for (const auto& handle_id : revoked) {
    records_.erase(handle_id);
  }
}

void ToolHandleBroker::RevokeAll() {
  records_.clear();
}

}  // namespace taffy
