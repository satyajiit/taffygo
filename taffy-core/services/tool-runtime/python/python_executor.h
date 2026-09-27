// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_EXECUTOR_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_EXECUTOR_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/memory/ref_counted.h"
#include "base/types/expected.h"

namespace taffy::python_tool {

enum class Failure {
  kInvalidInput,
  kResourceLimit,
  kDeadlineExceeded,
  kCancelled,
  kRuntimeFailure,
};

// Ref-counted because cancellation closes the worker's service pipe while the
// interpreter sequence may still be unwinding. The execution task owns one
// reference, so an orderly process teardown cannot leave its trace hook
// reading a member of an already-destroyed service.
using CancellationFlag = base::RefCountedData<std::atomic_bool>;

struct Request {
  std::string entrypoint;
  std::vector<uint8_t> input;
  base::File library;
  uint64_t library_bytes = 0;
  std::vector<uint8_t> library_digest;
  uint64_t max_output_bytes = 0;
  uint64_t max_memory_bytes = 0;
  uint64_t max_cpu_ms = 0;
};

using Result = base::expected<std::vector<uint8_t>, Failure>;

// Starts one isolated interpreter and destroys it before returning. The
// caller runs this on a worker sequence; cancellation is the only shared
// state, and a true value is observed from the interpreter's trace hook.
Result Execute(Request request, scoped_refptr<CancellationFlag> cancelled);

}  // namespace taffy::python_tool

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_EXECUTOR_H_
