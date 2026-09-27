// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/tool/python_tool_worker_sp07_test_support.h"

#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/threading/thread_restrictions.h"
#include "base/files/file_util.h"
#include "base/json/string_escape.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "crypto/hash.h"
#include "third_party/zlib/google/zip.h"

namespace taffy::python_sp07_test {
namespace {

constexpr char kAmbientSecret[] = "browser-owned ambient secret";
constexpr char kAmbientPathPlaceholder[] = "__TAFFY_AMBIENT_PATH__";

// This module is supplied only as the verified library descriptor of the
// process test. It does not add a callable entrypoint. The frozen
// taffy_python_tools module still owns dispatch, input validation and output.
constexpr char kProbeJsonModule[] = R"PY(
_AMBIENT_PATH = __TAFFY_AMBIENT_PATH__

class JSONDecodeError(ValueError):
    pass

def _blocked(call):
    try:
        value = call()
    except BaseException:
        return True
    try:
        close = getattr(value, "close", None)
        if close:
            close()
    except BaseException:
        pass
    return False

def _environment_hidden():
    try:
        environment = __import__("os").environ
        return environment.get("TAFFY_SP07_BROWSER_SECRET") is None
    except BaseException:
        return True

def _document(text):
    return {"document": {"title": "SP-07 probe", "sections": [{
        "heading": "Result", "paragraphs": [text]}]}}

def _sandbox_result():
    checks = (
        ("sys-path-empty", __import__("sys").path == []),
        ("ambient-file-blocked", _blocked(
            lambda: open(_AMBIENT_PATH, "rb"))),
        ("browser-environment-hidden", _environment_hidden()),
        ("network-native-blocked", _blocked(
            lambda: __import__("_socket"))),
        ("ctypes-native-blocked", _blocked(
            lambda: __import__("_ctypes"))),
        ("subprocess-native-blocked", _blocked(
            lambda: __import__("_posixsubprocess"))),
        ("subprocess-module-blocked", _blocked(
            lambda: __import__("subprocess"))),
        ("unallowlisted-module-blocked", _blocked(
            lambda: __import__("taffy_sp07_unallowlisted"))),
    )
    return _document(";".join(
        name + "=" + ("1" if passed else "0")
        for name, passed in checks))

def loads(value):
    if value == '"sandbox-probe"':
        return _sandbox_result()
    if value == '"memory-pressure"':
        allocation = bytearray(96 * 1024 * 1024)
        return _document(str(len(allocation)))
    if value == '"long-running"':
        counter = 0
        while True:
            counter += 1
    if value == '"recovery"':
        return _document("fresh-worker-recovered=1")
    raise JSONDecodeError("unexpected SP-07 test input")
)PY";

uint64_t MonotonicMilliseconds() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value > 0 ? static_cast<uint64_t>(value) : 0u;
}

}  // namespace

ProbeLibrary::ProbeLibrary() = default;
ProbeLibrary::~ProbeLibrary() = default;

bool ProbeLibrary::Initialize(const base::FilePath& root) {
  const base::FilePath source = root.AppendASCII("sp07-library");
  const base::FilePath ambient = root.AppendASCII("ambient-secret");
  if (!base::CreateDirectory(source) ||
      !base::WriteFile(ambient, kAmbientSecret)) {
    return false;
  }

  std::string module = kProbeJsonModule;
  base::ReplaceSubstringsAfterOffset(
      &module, 0u, kAmbientPathPlaceholder,
      base::GetQuotedJSONString(ambient.AsUTF8Unsafe()));
  if (!base::WriteFile(source.AppendASCII("json.py"), module)) {
    return false;
  }

  archive_path_ = root.AppendASCII("sp07-stdlib.zip");
  if (!zip::Zip(source, archive_path_, /*include_hidden_files=*/false)) {
    return false;
  }
  const std::optional<std::vector<uint8_t>> bytes =
      base::ReadFileToBytes(archive_path_);
  if (!bytes || bytes->empty()) {
    return false;
  }
  archive_bytes_ = bytes->size();
  const auto digest = crypto::hash::Sha256(base::span(*bytes));
  archive_digest_.assign(digest.begin(), digest.end());
  return true;
}

tool_runtime::mojom::ToolJobPtr ProbeLibrary::MakeJob(
    std::string job_id,
    std::string_view input,
    uint64_t max_memory_bytes,
    uint64_t max_cpu_ms) const {
  namespace mojom = tool_runtime::mojom;
  auto job = mojom::ToolJob::New();
  job->operation = mojom::OperationEnvelope::New(
      "operation-" + job_id, 1u, 1u, MonotonicMilliseconds() + 60'000u,
      "idempotency-" + job_id);
  job->job_id = std::move(job_id);
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ResourceBudget::New(262144u, 1u << 20, max_memory_bytes,
                                           max_cpu_ms, 0u, 1u);
  const auto input_bytes = base::as_byte_span(input);
  job->bundled_python = mojom::BundledPythonArguments::New(
      "document.build",
      std::vector<uint8_t>(input_bytes.begin(), input_bytes.end()));
  const auto input_digest = crypto::hash::Sha256(input_bytes);
  job->input_payload = mojom::ToolPayloadInput::New(
      mojom::ToolInputTransport::kInline, input_bytes.size(),
      std::vector<uint8_t>(input_digest.begin(), input_digest.end()));
  job->output_payload = mojom::ToolPayloadOutput::New(
      mojom::ToolOutputTransport::kInlineChunks, 1u << 20);
  job->python_library = mojom::ToolPythonLibrary::New(
      "python-stdlib", "sp07-test-1", archive_bytes_, archive_digest_);
  return job;
}

tool_runtime::mojom::ToolJobResourcesPtr ProbeLibrary::OpenResources() const {
  // A browser test runs its body on the UI thread, where blocking is
  // disallowed, and opening the archive is a blocking call — so every test that
  // asked for these resources aborted its browser process here rather than
  // exercising the worker. Production opens this file off the UI thread; only
  // the test hands it over from the test body, so the scope belongs here and
  // not around the worker's own open.
  base::ScopedAllowBlockingForTesting allow_blocking;
  auto resources = tool_runtime::mojom::ToolJobResources::New();
  resources->python_library =
      base::File(archive_path_, base::File::FLAG_OPEN | base::File::FLAG_READ);
  return resources;
}

bool OutputContains(base::span<const uint8_t> output,
                    std::string_view expected) {
  return std::string_view(reinterpret_cast<const char*>(output.data()),
                          output.size())
             .find(expected) != std::string_view::npos;
}

}  // namespace taffy::python_sp07_test
