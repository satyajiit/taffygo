// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_MODEL_SCRIPT_H_
#define TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_MODEL_SCRIPT_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

namespace taffy::test {

// One local, deterministic candidate script for TB-302. The response still
// traverses the shipping model broker and isolated task runtime, but the URL
// loader interceptor guarantees that no provider or other external host sees
// the fixture content. This is diagnostic evidence pending corpus ratification,
// never a frozen benchmark model output.
class TaskBenchmarkModelScript final {
 public:
  TaskBenchmarkModelScript();
  TaskBenchmarkModelScript(const TaskBenchmarkModelScript&) = delete;
  TaskBenchmarkModelScript& operator=(const TaskBenchmarkModelScript&) = delete;
  ~TaskBenchmarkModelScript();

  std::string base_url() const;
  uint32_t request_count() const;
  bool invalid_request_seen() const;
  std::optional<uint32_t> selected_handle() const;
  std::string request_sha256() const;
  std::string response_sha256() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Public only for a deterministic parser regression. Production benchmark
// execution reaches the same function through TaskBenchmarkModelScript.
std::optional<uint32_t> FindStableLinkHandleForTesting(
    const std::string& request_body);

}  // namespace taffy::test

#endif  // TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_MODEL_SCRIPT_H_
