// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser's own half of the frozen entrypoint registry, driven through the
// edge that actually runs: `IsValidCoreToolJob` is what stands between a job
// the sandboxed core proposed and a process this build starts.
//
// Two of the assertions here would have been impossible to write before the
// registry existed, because before it the check was "is this string shaped like
// an identifier", and every string of the right shape passed. The refusal
// against a natively-owned row is the one to read twice: the row is in the
// table, the identifier is perfect, and the answer is still no (decision 0064).

#include "taffy/browser/core_tool_validation.h"

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/components/tools/entrypoints/tool_entrypoint_registry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr size_t kMaxIdentifierBytes = 256u;
constexpr size_t kMaxEffectBytes = 64u * 1024u;

mojom::ToolJobEffectPtr PythonJob(const std::string& entrypoint_id) {
  auto job = mojom::ToolJobEffect::New();
  job->job_id = "job-1";
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->tool_id = "bundled-tool";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ToolResourceBudget::New(1024u, 1024u, 1024u * 1024u,
                                               1000u, 4096u, 4u);
  job->bundled_python =
      mojom::BundledPythonArguments::New(entrypoint_id, std::vector<uint8_t>{1u});
  job->task_id = "task-1";
  return job;
}

// The admitted half first, so every refusal below is a refusal of something
// this edge would otherwise have passed.
TEST(CoreToolEntrypointTest, ARegisteredEntrypointWithNoNativeOwnerIsAdmitted) {
  for (const char* entrypoint : {"document.build", "spreadsheet.build"}) {
    EXPECT_TRUE(IsValidCoreToolJob(*PythonJob(entrypoint), kMaxIdentifierBytes,
                                   kMaxEffectBytes))
        << entrypoint;
  }
}

// The rule the registry exists for. `table.reshape` is a row, its identifier is
// well formed, and the browser still refuses to start a worker for it, because
// reshaping rows is arithmetic the portable core owns.
TEST(CoreToolEntrypointTest, ANativelyOwnedEntrypointNeverStartsAWorker) {
  EXPECT_FALSE(IsValidCoreToolJob(*PythonJob("table.reshape"),
                                  kMaxIdentifierBytes, kMaxEffectBytes));

  const tools::ToolEntrypointVerdict verdict =
      tools::AdmitToolEntrypoint("table.reshape");
  EXPECT_EQ(verdict.admission,
            tools::ToolEntrypointAdmission::kRefusedForNativePath);
  EXPECT_EQ(verdict.native_alternative, "core.table.reshape");
}

// A name the build was never compiled with, and four near misses. The near
// misses matter more: a lookup that trimmed, lowercased or prefix-matched would
// be a lookup the core could steer into a row it was not given.
TEST(CoreToolEntrypointTest, AnUnregisteredEntrypointIsRefused) {
  for (const char* entrypoint : {"shell.run", "table", "table.reshap",
                                 "Table.reshape", " table.reshape"}) {
    EXPECT_FALSE(IsValidCoreToolJob(*PythonJob(entrypoint), kMaxIdentifierBytes,
                                    kMaxEffectBytes))
        << entrypoint;
    EXPECT_EQ(tools::AdmitToolEntrypoint(entrypoint).admission,
              tools::ToolEntrypointAdmission::kUnknown)
        << entrypoint;
  }
}

// The table this build compiled in, asserted as a vector rather than a count.
// A count passes when one row is added and another removed, which is the review
// this assertion exists to force.
TEST(CoreToolEntrypointTest, TheCompiledInSurfaceIsExactlyThreeNamedRows) {
  std::vector<std::string> ids;
  for (const tools::ToolEntrypoint& row : tools::kToolEntrypoints) {
    ids.emplace_back(row.id);
  }
  EXPECT_EQ(ids, (std::vector<std::string>{"document.build",
                                           "spreadsheet.build",
                                           "table.reshape"}));
  EXPECT_EQ(tools::kToolRegistryVersion, 1u);
  EXPECT_EQ(tools::kToolRegistryFingerprint, 1879083464u);
}

// The C++ and Rust halves are generated from one source, so they cannot
// disagree about the rows. What they can disagree about is which rows carry a
// native owner, because that is the field a well-meaning edit removes to make
// something work. Exactly one row is refused, and it is named.
TEST(CoreToolEntrypointTest, ExactlyOneRowIsRefusedInFavourOfANativePath) {
  std::vector<std::string> refused;
  for (const tools::ToolEntrypoint& row : tools::kToolEntrypoints) {
    if (!row.native_alternative.empty()) {
      refused.emplace_back(row.native_alternative);
    }
  }
  EXPECT_EQ(refused, (std::vector<std::string>{"core.table.reshape"}));
  ASSERT_EQ(tools::kToolNativePaths.size(), 1u);
  EXPECT_EQ(tools::kToolNativePaths.front().state,
            tools::ToolNativeState::kActive);
}

// Every declared port is bounded and described. The identity gate above is only
// half the bound: an entrypoint that could declare a port of an open kind could
// be handed anything once its name was admitted.
TEST(CoreToolEntrypointTest, EveryPortIsNamedBoundedAndDescribed) {
  for (const tools::ToolEntrypoint& row : tools::kToolEntrypoints) {
    EXPECT_FALSE(row.summary.empty()) << row.id;
    EXPECT_FALSE(row.inputs.empty()) << row.id;
    EXPECT_FALSE(row.outputs.empty()) << row.id;
    for (const tools::ToolEntrypointPort& port : row.inputs) {
      EXPECT_FALSE(port.name.empty()) << row.id;
      EXPECT_FALSE(port.description.empty()) << row.id;
    }
    for (const tools::ToolEntrypointPort& port : row.outputs) {
      EXPECT_FALSE(port.name.empty()) << row.id;
      EXPECT_FALSE(port.description.empty()) << row.id;
    }
  }
}

// The signed-WASM family carries an entrypoint of its own and is deliberately
// not held to this table: the rows say what a Python worker may do, and
// admitting them for a second runtime would be the registry claiming something
// it never said. That runtime has no service and no registry yet, and this test
// records which of the two it is.
TEST(CoreToolEntrypointTest, TheWasmFamilyIsNotHeldToThePythonRegistry) {
  auto job = PythonJob("unused");
  job->runtime = mojom::ToolRuntimeKind::kWasm;
  job->operation_kind = mojom::ToolOperation::kRunSignedWasmTransform;
  job->bundled_python.reset();
  job->signed_wasm =
      mojom::SignedWasmArguments::New("transform", std::vector<uint8_t>{1u});
  EXPECT_TRUE(
      IsValidCoreToolJob(*job, kMaxIdentifierBytes, kMaxEffectBytes));
}

}  // namespace
}  // namespace taffy
