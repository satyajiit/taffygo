// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_LIBRARY_MEMORY_VERTICAL_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_LIBRARY_MEMORY_VERTICAL_TEST_SUPPORT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/test/recovery/core_api_status_observer.h"

namespace taffy::test {

// A deterministic external endpoint for the production model broker. It
// supplies provider-shaped streamed replies at the network boundary only;
// every task, policy, tool, service, storage, and status transition remains
// the shipping implementation in the isolated core and browser process.
class LibraryMemoryTaskModelEndpoint final {
 public:
  LibraryMemoryTaskModelEndpoint(std::string workspace_id,
                                 uint64_t workspace_revision,
                                 std::string fact_id,
                                 std::string library_query,
                                 std::string memory_statement,
                                 std::string memory_query);
  LibraryMemoryTaskModelEndpoint(const LibraryMemoryTaskModelEndpoint&) =
      delete;
  LibraryMemoryTaskModelEndpoint& operator=(
      const LibraryMemoryTaskModelEndpoint&) = delete;
  ~LibraryMemoryTaskModelEndpoint();

  std::string base_url() const;
  uint32_t request_count() const;
  bool invalid_request_seen() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

std::optional<ObservedLibrarySearch> RequestObservedLibrarySearch(
    mojo::Remote<core_api::mojom::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_id,
    const std::string& query,
    uint64_t expected_revision,
    const std::string& expected_entry_id);

std::optional<ObservedMemorySearch> RequestObservedMemorySearch(
    mojo::Remote<core_api::mojom::TaffyProfileCoreApi>* facade,
    CoreApiStatusObserver* observer,
    const std::string& request_id,
    const std::string& query,
    uint64_t expected_revision,
    const std::string& expected_memory_id);

void ExpectRestoredLibrary(const ObservedLibraryStatus& expected,
                           const ObservedLibraryStatus& actual);
void ExpectRestoredMemory(const ObservedMemoryStatus& expected,
                          const ObservedMemoryStatus& actual);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_LIBRARY_MEMORY_VERTICAL_TEST_SUPPORT_H_
