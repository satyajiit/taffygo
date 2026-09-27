// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_MODEL_ENDPOINT_H_
#define TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_MODEL_ENDPOINT_H_

#include <cstdint>
#include <memory>
#include <string>

namespace taffy::test {

bool HasBothComparisonPagesForTesting(const std::string& request_body);

// A network-only reply. Its request must contain both real page projections;
// no observation, task state, provider result, or source identity is injected.
class ComparisonVerticalModelEndpoint final {
 public:
  ComparisonVerticalModelEndpoint();
  ~ComparisonVerticalModelEndpoint();
  std::string base_url() const;
  uint32_t request_count() const;
  bool invalid_request_seen() const;
  bool both_pages_seen() const;

 private:
  class Impl;
  const std::unique_ptr<Impl> impl_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_MODEL_ENDPOINT_H_
