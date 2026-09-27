// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/benchmark/task_benchmark_model_script.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "content/public/test/url_loader_interceptor.h"
#include "crypto/sha2.h"
#include "services/network/test/test_utils.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

constexpr char kBaseUrl[] = "https://task-benchmark-script.test/v1";
constexpr char kRequestUrl[] =
    "https://task-benchmark-script.test/v1/chat/completions";

std::string Sha256(std::string_view value) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(value)));
}

std::optional<uint32_t> HandleInProjectionLine(std::string_view line) {
  if (line.find("never mutates") == std::string_view::npos) {
    return std::nullopt;
  }
  const size_t open = line.find('[');
  const size_t close =
      line.find(']', open == std::string_view::npos ? 0u : open);
  if (open == std::string_view::npos || close == std::string_view::npos ||
      close <= open + 1u) {
    return std::nullopt;
  }
  uint32_t handle = 0u;
  return base::StringToUint(line.substr(open + 1u, close - open - 1u), &handle)
             ? std::make_optional(handle)
             : std::nullopt;
}

void CollectStableHandles(const base::Value& value,
                          std::vector<uint32_t>* handles) {
  if (value.is_string()) {
    for (std::string_view line :
         base::SplitStringPiece(value.GetString(), "\n", base::KEEP_WHITESPACE,
                                base::SPLIT_WANT_ALL)) {
      if (const std::optional<uint32_t> handle = HandleInProjectionLine(line)) {
        handles->push_back(*handle);
      }
    }
    return;
  }
  if (value.is_list()) {
    for (const base::Value& child : value.GetList()) {
      CollectStableHandles(child, handles);
    }
    return;
  }
  if (value.is_dict()) {
    for (const auto item : value.GetDict()) {
      CollectStableHandles(item.second, handles);
    }
  }
}

std::string Encode(base::DictValue value) {
  std::string encoded;
  CHECK(base::JSONWriter::Write(value, &encoded));
  return encoded;
}

std::string ToolReply(uint32_t handle) {
  base::DictValue arguments;
  arguments.Set("node", static_cast<int>(handle));
  base::DictValue function;
  function.Set("name", "browser.link.open");
  function.Set("arguments", Encode(std::move(arguments)));
  base::DictValue call;
  call.Set("index", 0);
  call.Set("id", "tb302-link-open");
  call.Set("type", "function");
  call.Set("function", std::move(function));
  base::ListValue calls;
  calls.Append(std::move(call));
  base::DictValue delta;
  delta.Set("tool_calls", std::move(calls));
  base::DictValue choice;
  choice.Set("delta", std::move(delta));
  choice.Set("finish_reason", "tool_calls");
  base::ListValue choices;
  choices.Append(std::move(choice));
  base::DictValue usage;
  usage.Set("prompt_tokens", 1);
  usage.Set("completion_tokens", 1);
  base::DictValue root;
  root.Set("choices", std::move(choices));
  root.Set("usage", std::move(usage));
  return base::StrCat(
      {"data: ", Encode(std::move(root)), "\n\ndata: [DONE]\n\n"});
}

}  // namespace

std::optional<uint32_t> FindStableLinkHandleForTesting(
    const std::string& request_body) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(request_body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  std::vector<uint32_t> handles;
  CollectStableHandles(*parsed, &handles);
  std::ranges::sort(handles);
  handles.erase(std::ranges::unique(handles).begin(), handles.end());
  return handles.size() == 1u ? std::make_optional(handles.front())
                              : std::nullopt;
}

class TaskBenchmarkModelScript::Impl final {
 public:
  Impl()
      : interceptor_(std::make_unique<content::URLLoaderInterceptor>(
            base::BindRepeating(&Impl::Intercept, base::Unretained(this)))) {}

  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    if (params->url_request.url != GURL(kRequestUrl)) {
      return false;
    }
    request_count_.fetch_add(1u, std::memory_order_relaxed);
    const std::string body = network::GetUploadData(params->url_request);
    const std::optional<uint32_t> handle = FindStableLinkHandleForTesting(body);
    const std::optional<std::string> content_type =
        params->url_request.headers.GetHeader("Content-Type");
    const bool valid =
        params->url_request.method == "POST" &&
        params->url_request.request_body &&
        content_type == "application/json" &&
        !params->url_request.headers.HasHeader("Authorization") &&
        handle.has_value() && request_count() == 1u;
    if (!valid) {
      invalid_request_seen_.store(true, std::memory_order_relaxed);
    }
    if (handle) {
      selected_handle_.store(*handle, std::memory_order_relaxed);
      selected_handle_seen_.store(true, std::memory_order_release);
    }
    const std::string reply = ToolReply(handle.value_or(0u));
    {
      base::AutoLock lock(digests_lock_);
      request_sha256_ = Sha256(body);
      response_sha256_ = Sha256(reply);
    }
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 200 OK\nContent-Type: text/event-stream\n", reply,
        params->client.get(), std::nullopt, GURL(kRequestUrl));
    return true;
  }

  uint32_t request_count() const {
    return request_count_.load(std::memory_order_relaxed);
  }

  bool invalid_request_seen() const {
    return invalid_request_seen_.load(std::memory_order_relaxed);
  }

  std::optional<uint32_t> selected_handle() const {
    return selected_handle_seen_.load(std::memory_order_acquire)
               ? std::make_optional(
                     selected_handle_.load(std::memory_order_relaxed))
               : std::nullopt;
  }

  std::string request_sha256() const {
    base::AutoLock lock(digests_lock_);
    return request_sha256_;
  }

  std::string response_sha256() const {
    base::AutoLock lock(digests_lock_);
    return response_sha256_;
  }

 private:
  std::atomic<uint32_t> request_count_{0u};
  std::atomic<bool> invalid_request_seen_{false};
  std::atomic<uint32_t> selected_handle_{0u};
  std::atomic<bool> selected_handle_seen_{false};
  mutable base::Lock digests_lock_;
  std::string request_sha256_ GUARDED_BY(digests_lock_);
  std::string response_sha256_ GUARDED_BY(digests_lock_);
  std::unique_ptr<content::URLLoaderInterceptor> interceptor_;
};

TaskBenchmarkModelScript::TaskBenchmarkModelScript()
    : impl_(std::make_unique<Impl>()) {}

TaskBenchmarkModelScript::~TaskBenchmarkModelScript() = default;

std::string TaskBenchmarkModelScript::base_url() const {
  return kBaseUrl;
}

uint32_t TaskBenchmarkModelScript::request_count() const {
  return impl_->request_count();
}

bool TaskBenchmarkModelScript::invalid_request_seen() const {
  return impl_->invalid_request_seen();
}

std::optional<uint32_t> TaskBenchmarkModelScript::selected_handle() const {
  return impl_->selected_handle();
}

std::string TaskBenchmarkModelScript::request_sha256() const {
  return impl_->request_sha256();
}

std::string TaskBenchmarkModelScript::response_sha256() const {
  return impl_->response_sha256();
}

}  // namespace taffy::test
