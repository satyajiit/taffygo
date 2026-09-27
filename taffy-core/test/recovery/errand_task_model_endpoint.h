// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_ERRAND_TASK_MODEL_ENDPOINT_H_
#define TAFFY_TEST_RECOVERY_ERRAND_TASK_MODEL_ENDPOINT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace taffy::test {

// Reads only the current model-visible user projection, never tool history,
// and refuses a missing or ambiguous Download link. No handle is invented.
std::optional<uint32_t> FindErrandDownloadLinkHandleForTesting(
    const std::string& request_body);

// Only the latest tool result, bound to its download.list call, can say
// complete (true) or still in progress (false). Every other shape refuses.
std::optional<bool> LatestErrandDownloadCompletionForTesting(
    const std::string& request_body);

// Deterministic provider-shaped replies at the network boundary only. The
// real core drives navigation, handover, fresh observation and the download.
class ErrandTaskModelEndpoint final {
 public:
  static constexpr char kPrivateInput[] = "private-fixture-entry-42";
  static constexpr uint32_t kMaxRequests = 16u;

  explicit ErrandTaskModelEndpoint(std::string navigation_url);
  ErrandTaskModelEndpoint(const ErrandTaskModelEndpoint&) = delete;
  ErrandTaskModelEndpoint& operator=(const ErrandTaskModelEndpoint&) = delete;
  ~ErrandTaskModelEndpoint();

  std::string base_url() const;
  uint32_t request_count() const;
  bool invalid_request_seen() const;
  bool private_input_seen() const;
  std::optional<uint32_t> selected_handle() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_ERRAND_TASK_MODEL_ENDPOINT_H_
