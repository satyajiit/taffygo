// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_TOOL_PYTHON_TOOL_WORKER_SP07_TEST_SUPPORT_H_
#define TAFFY_TEST_TOOL_PYTHON_TOOL_WORKER_SP07_TEST_SUPPORT_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"

namespace taffy::python_sp07_test {

inline constexpr char kBrowserSecretEnvironmentName[] =
    "TAFFY_SP07_BROWSER_SECRET";
inline constexpr char kSandboxProbeInput[] = R"("sandbox-probe")";
inline constexpr char kMemoryPressureInput[] = R"("memory-pressure")";
inline constexpr char kLongRunningInput[] = R"("long-running")";
inline constexpr char kRecoveryInput[] = R"("recovery")";

// A browser-owned, descriptor-backed library made specifically for the SP-07
// process test. It cannot replace the frozen dispatch table: each input still
// enters through the shipping document.build entrypoint. The custom json
// module only makes otherwise invisible sandbox observations content-free and
// deterministic enough to return in that document's bytes.
class ProbeLibrary {
 public:
  ProbeLibrary();
  ProbeLibrary(const ProbeLibrary&) = delete;
  ProbeLibrary& operator=(const ProbeLibrary&) = delete;
  ~ProbeLibrary();

  bool Initialize(const base::FilePath& root);

  tool_runtime::mojom::ToolJobPtr MakeJob(std::string job_id,
                                          std::string_view input,
                                          uint64_t max_memory_bytes,
                                          uint64_t max_cpu_ms) const;
  tool_runtime::mojom::ToolJobResourcesPtr OpenResources() const;

 private:
  base::FilePath archive_path_;
  uint64_t archive_bytes_ = 0u;
  std::vector<uint8_t> archive_digest_;
};

bool OutputContains(base::span<const uint8_t> output,
                    std::string_view expected);

}  // namespace taffy::python_sp07_test

#endif  // TAFFY_TEST_TOOL_PYTHON_TOOL_WORKER_SP07_TEST_SUPPORT_H_
