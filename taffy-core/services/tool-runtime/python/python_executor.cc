// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// CPython's stable embedding headers and allocator ABI expose pointer-sized
// arrays directly. Keep that unavoidable C boundary confined to this file.
#ifdef UNSAFE_BUFFERS_BUILD
#pragma allow_unsafe_buffers
#endif

#include "taffy/services/tool-runtime/python/python_executor.h"

#include <Python.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

#include "base/containers/span.h"
#include "base/files/memory_mapped_file.h"
#include "base/time/time.h"
#include "crypto/hash.h"
#include "taffy_frozen.h"

namespace taffy::python_tool {
namespace {

struct AllocatorState {
  std::atomic_size_t used{0u};
  size_t limit = 0u;
};

struct alignas(std::max_align_t) AllocationHeader {
  size_t bytes;
};

struct ExecutionState {
  std::reference_wrapper<const std::atomic_bool> cancelled;
  base::ThreadTicks started;
  base::TimeDelta cpu_limit;
};

thread_local ExecutionState* g_execution = nullptr;

void* BudgetMalloc(void* context, size_t requested) {
  auto* state = static_cast<AllocatorState*>(context);
  const size_t bytes = std::max(requested, size_t{1});
  if (bytes > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader)) {
    return nullptr;
  }
  const size_t allocation_bytes = sizeof(AllocationHeader) + bytes;
  size_t used = state->used.load(std::memory_order_relaxed);
  do {
    if (allocation_bytes > state->limit ||
        used > state->limit - allocation_bytes) {
      return nullptr;
    }
  } while (!state->used.compare_exchange_weak(used, used + allocation_bytes,
                                              std::memory_order_relaxed));
  auto* header = static_cast<AllocationHeader*>(std::malloc(allocation_bytes));
  if (!header) {
    state->used.fetch_sub(allocation_bytes, std::memory_order_relaxed);
    return nullptr;
  }
  header->bytes = allocation_bytes;
  return header + 1;
}

void* BudgetCalloc(void* context, size_t count, size_t item_size) {
  if (item_size && count > std::numeric_limits<size_t>::max() / item_size) {
    return nullptr;
  }
  const size_t bytes = count * item_size;
  void* result = BudgetMalloc(context, bytes);
  if (result) {
    std::memset(result, 0, std::max(bytes, size_t{1}));
  }
  return result;
}

void BudgetFree(void* context, void* pointer) {
  if (!pointer) {
    return;
  }
  auto* header = static_cast<AllocationHeader*>(pointer) - 1;
  static_cast<AllocatorState*>(context)->used.fetch_sub(
      header->bytes, std::memory_order_relaxed);
  std::free(header);
}

void* BudgetRealloc(void* context, void* pointer, size_t requested) {
  if (!pointer) {
    return BudgetMalloc(context, requested);
  }
  auto* state = static_cast<AllocatorState*>(context);
  auto* old_header = static_cast<AllocationHeader*>(pointer) - 1;
  const size_t old_allocation_bytes = old_header->bytes;
  const size_t bytes = std::max(requested, size_t{1});
  if (bytes > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader)) {
    return nullptr;
  }
  const size_t allocation_bytes = sizeof(AllocationHeader) + bytes;
  if (allocation_bytes > old_allocation_bytes) {
    const size_t growth = allocation_bytes - old_allocation_bytes;
    size_t used = state->used.load(std::memory_order_relaxed);
    do {
      if (growth > state->limit || used > state->limit - growth) {
        return nullptr;
      }
    } while (!state->used.compare_exchange_weak(used, used + growth,
                                                std::memory_order_relaxed));
  }
  auto* header = static_cast<AllocationHeader*>(
      std::realloc(old_header, allocation_bytes));
  if (!header) {
    if (allocation_bytes > old_allocation_bytes) {
      state->used.fetch_sub(allocation_bytes - old_allocation_bytes,
                            std::memory_order_relaxed);
    }
    return nullptr;
  }
  if (old_allocation_bytes > allocation_bytes) {
    state->used.fetch_sub(old_allocation_bytes - allocation_bytes,
                          std::memory_order_relaxed);
  }
  header->bytes = allocation_bytes;
  return header + 1;
}

void InstallAllocator(AllocatorState* state) {
  PyMemAllocatorEx allocator = {state, BudgetMalloc, BudgetCalloc,
                                BudgetRealloc, BudgetFree};
  PyMem_SetAllocator(PYMEM_DOMAIN_RAW, &allocator);
  PyMem_SetAllocator(PYMEM_DOMAIN_MEM, &allocator);
  PyMem_SetAllocator(PYMEM_DOMAIN_OBJ, &allocator);
}

int Trace(PyObject*, PyFrameObject*, int, PyObject*) {
  if (!g_execution) {
    return 0;
  }
  if (g_execution->cancelled.get().load(std::memory_order_relaxed)) {
    PyErr_SetString(PyExc_KeyboardInterrupt, "tool job cancelled");
    return -1;
  }
  if (base::ThreadTicks::Now() - g_execution->started >
      g_execution->cpu_limit) {
    PyErr_SetString(PyExc_TimeoutError, "tool job exceeded its CPU budget");
    return -1;
  }
  return 0;
}

Failure CurrentFailure() {
  if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
    return Failure::kCancelled;
  }
  if (PyErr_ExceptionMatches(PyExc_TimeoutError)) {
    return Failure::kDeadlineExceeded;
  }
  if (PyErr_ExceptionMatches(PyExc_MemoryError)) {
    return Failure::kResourceLimit;
  }
  if (PyErr_ExceptionMatches(PyExc_ValueError) ||
      PyErr_ExceptionMatches(PyExc_UnicodeError)) {
    return Failure::kInvalidInput;
  }
  return Failure::kRuntimeFailure;
}

bool InstallLibrary(base::span<const uint8_t> archive) {
  PyObject* module = PyImport_ImportModule("taffy_stdlib_boot");
  if (!module) {
    return false;
  }
  PyObject* view = PyMemoryView_FromMemory(
      reinterpret_cast<char*>(const_cast<uint8_t*>(archive.data())),
      static_cast<Py_ssize_t>(archive.size()), PyBUF_READ);
  PyObject* result =
      view ? PyObject_CallMethod(module, "install", "O", view) : nullptr;
  Py_XDECREF(result);
  Py_XDECREF(view);
  Py_DECREF(module);
  return result != nullptr;
}

Result Invoke(const Request& request) {
  PyObject* module = PyImport_ImportModule("taffy_python_tools");
  PyObject* function =
      module ? PyObject_GetAttrString(module, "execute") : nullptr;
  PyObject* entrypoint =
      function ? PyUnicode_FromStringAndSize(
                     request.entrypoint.data(),
                     static_cast<Py_ssize_t>(request.entrypoint.size()))
               : nullptr;
  PyObject* input =
      entrypoint ? PyBytes_FromStringAndSize(
                       reinterpret_cast<const char*>(request.input.data()),
                       static_cast<Py_ssize_t>(request.input.size()))
                 : nullptr;
  PyObject* output =
      input ? PyObject_CallFunctionObjArgs(function, entrypoint, input, nullptr)
            : nullptr;
  Py_XDECREF(input);
  Py_XDECREF(entrypoint);
  Py_XDECREF(function);
  Py_XDECREF(module);
  if (!output) {
    return base::unexpected(CurrentFailure());
  }
  if (!PyBytes_Check(output)) {
    Py_DECREF(output);
    return base::unexpected(Failure::kRuntimeFailure);
  }
  const Py_ssize_t size = PyBytes_GET_SIZE(output);
  if (size < 0 || static_cast<uint64_t>(size) > request.max_output_bytes) {
    Py_DECREF(output);
    return base::unexpected(Failure::kResourceLimit);
  }
  const auto* begin =
      reinterpret_cast<const uint8_t*>(PyBytes_AS_STRING(output));
  std::vector<uint8_t> bytes(
      base::span(begin, static_cast<size_t>(size)).begin(),
      base::span(begin, static_cast<size_t>(size)).end());
  Py_DECREF(output);
  return bytes;
}

}  // namespace

Result Execute(Request request, scoped_refptr<CancellationFlag> cancelled) {
  if (!cancelled) {
    return base::unexpected(Failure::kRuntimeFailure);
  }
  base::MemoryMappedFile library;
  if (!request.library.IsValid() ||
      !library.Initialize(std::move(request.library)) ||
      library.length() != request.library_bytes ||
      !std::ranges::equal(crypto::hash::Sha256(library.bytes()),
                          request.library_digest)) {
    return base::unexpected(Failure::kInvalidInput);
  }
  if (!base::ThreadTicks::IsSupported()) {
    return base::unexpected(Failure::kRuntimeFailure);
  }

  AllocatorState allocator;
  allocator.limit = static_cast<size_t>(request.max_memory_bytes);
  InstallAllocator(&allocator);
  PyImport_FrozenModules = taffy_frozen_extra;

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);
  config.module_search_paths_set = 1;
  config.site_import = 0;
  config.user_site_directory = 0;
  config.write_bytecode = 0;
  config.install_signal_handlers = 0;
  config.pathconfig_warnings = 0;
  config.parse_argv = 0;
  config.use_environment = 0;
  config.safe_path = 1;
  config.use_hash_seed = 1;
  config.hash_seed = 0;
  PyStatus status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(status)) {
    return base::unexpected(allocator.used.load() >= allocator.limit
                                ? Failure::kResourceLimit
                                : Failure::kRuntimeFailure);
  }

  ExecutionState execution{cancelled->data, base::ThreadTicks::Now(),
                           base::Milliseconds(request.max_cpu_ms)};
  g_execution = &execution;
  PyEval_SetTrace(Trace, nullptr);
  Result result = InstallLibrary(library.bytes())
                      ? Invoke(request)
                      : Result(base::unexpected(CurrentFailure()));
  PyEval_SetTrace(nullptr, nullptr);
  g_execution = nullptr;
  if (Py_FinalizeEx() < 0 && result.has_value()) {
    return base::unexpected(Failure::kRuntimeFailure);
  }
  return result;
}

}  // namespace taffy::python_tool
