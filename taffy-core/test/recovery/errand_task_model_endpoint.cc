// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/errand_task_model_endpoint.h"

#include <atomic>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "content/public/test/url_loader_interceptor.h"
#include "services/network/test/test_utils.h"
#include "taffy/test/recovery/task_model_reply_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

constexpr char kBaseUrl[] = "https://errand-model.test/v1";
constexpr char kRequestUrl[] = "https://errand-model.test/v1/chat/completions";

std::optional<uint32_t> DownloadLinkInLine(std::string_view line) {
  if (!line.starts_with('[')) {
    return std::nullopt;
  }
  const size_t close = line.find(']');
  if (close == std::string_view::npos ||
      !line.substr(close).starts_with("] link \"")) {
    return std::nullopt;
  }
  const size_t label_start = close + std::string_view("] link \"").size();
  const size_t label_end = line.find("\" (authored by ", label_start);
  if (label_end == std::string_view::npos ||
      base::ToLowerASCII(line.substr(label_start, label_end - label_start))
              .find("download") == std::string::npos) {
    return std::nullopt;
  }
  uint32_t handle = 0u;
  return base::StringToUint(line.substr(1u, close - 1u), &handle) &&
                 handle <=
                     static_cast<uint32_t>(std::numeric_limits<int>::max())
             ? std::make_optional(handle)
             : std::nullopt;
}

}  // namespace

std::optional<uint32_t> FindErrandDownloadLinkHandleForTesting(
    const std::string& request_body) {
  const auto parsed =
      base::JSONReader::ReadDict(request_body, base::JSON_PARSE_RFC);
  const auto* messages = parsed ? parsed->FindList("messages") : nullptr;
  if (!messages) {
    return std::nullopt;
  }
  std::vector<uint32_t> handles;
  for (const auto& message : *messages) {
    const auto* dict = message.GetIfDict();
    const auto* role = dict ? dict->FindString("role") : nullptr;
    const auto* text = dict ? dict->FindString("content") : nullptr;
    // The current page is injected into the user opening turn. Replayed
    // tool results and assistant arguments cannot supply a current handle.
    if (!role || *role != "user" || !text) {
      continue;
    }
    for (std::string_view line : base::SplitStringPiece(
             *text, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
      if (const auto handle = DownloadLinkInLine(line)) {
        handles.push_back(*handle);
      }
    }
  }
  return handles.size() == 1u ? std::make_optional(handles.front())
                              : std::nullopt;
}

std::optional<bool> LatestErrandDownloadCompletionForTesting(
    const std::string& request_body) {
  const auto parsed =
      base::JSONReader::ReadDict(request_body, base::JSON_PARSE_RFC);
  const auto* messages = parsed ? parsed->FindList("messages") : nullptr;
  if (!messages) {
    return std::nullopt;
  }
  for (size_t index = messages->size(); index > 0u; --index) {
    const auto* tool = (*messages)[index - 1u].GetIfDict();
    const auto* role = tool ? tool->FindString("role") : nullptr;
    if (!role) {
      return std::nullopt;
    }
    if (*role != "tool") {
      continue;
    }
    const auto* text = tool->FindString("content");
    const auto* call_id = tool->FindString("tool_call_id");
    if (!text || !call_id || call_id->empty() || index < 2u) {
      return std::nullopt;
    }
    // The fixture emits exactly one call per turn. Bind the latest result
    // to that call; older results and prose cannot certify this poll.
    const auto* assistant = (*messages)[index - 2u].GetIfDict();
    const auto* assistant_role =
        assistant ? assistant->FindString("role") : nullptr;
    const auto* calls = assistant ? assistant->FindList("tool_calls") : nullptr;
    const auto* call =
        calls && calls->size() == 1u ? calls->front().GetIfDict() : nullptr;
    const auto* id = call ? call->FindString("id") : nullptr;
    const auto* function = call ? call->FindDict("function") : nullptr;
    const auto* name = function ? function->FindString("name") : nullptr;
    if (!assistant_role || *assistant_role != "assistant" || !id ||
        *id != *call_id || !name || *name != "browser.download.list") {
      return std::nullopt;
    }
    // Tool transcript pieces may be separated by paragraph breaks. Accept
    // only the exact single-file result, independent of those separators.
    std::string compact;
    base::RemoveChars(*text, base::kWhitespaceASCII, &compact);
    if (compact == "Downloads:downloadone(complete).") {
      return true;
    }
    if (compact == "Downloads:downloadone(inprogress).") {
      return false;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

class ErrandTaskModelEndpoint::Impl final {
 public:
  explicit Impl(std::string navigation_url)
      : navigation_url_(std::move(navigation_url)),
        interceptor_(std::make_unique<content::URLLoaderInterceptor>(
            base::BindRepeating(&Impl::Intercept, base::Unretained(this)))) {}

  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    if (params->url_request.url != GURL(kRequestUrl)) {
      return false;
    }
    const uint32_t ordinal = request_count_.fetch_add(1u) + 1u;
    const auto& request = params->url_request;
    const std::string body =
        request.request_body ? network::GetUploadData(request) : std::string();
    const bool private_input = body.find(kPrivateInput) != std::string::npos;
    if (private_input) {
      private_input_seen_.store(true);
    }
    const auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
    const bool valid =
        request.method == "POST" && request.request_body &&
        request.headers.GetHeader("Content-Type") == "application/json" &&
        !request.headers.HasHeader("Authorization") && parsed &&
        parsed->FindBool("stream").value_or(false) &&
        parsed->FindList("messages") && !private_input;
    if (!valid || ordinal > kMaxRequests || final_reply_sent_.load()) {
      Refuse(params, ordinal, "unexpected request shape, disclosure, or count");
      return true;
    }
    std::optional<std::string> reply = Reply(ordinal, body);
    if (!reply) {
      Refuse(params, ordinal,
             "current Download link or latest list result is unusable");
      return true;
    }
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 200 OK\nContent-Type: text/event-stream\n", *reply,
        params->client.get(), std::nullopt, GURL(kRequestUrl));
    return true;
  }

  uint32_t request_count() const { return request_count_.load(); }
  bool invalid_request_seen() const { return invalid_request_seen_.load(); }
  bool private_input_seen() const { return private_input_seen_.load(); }
  std::optional<uint32_t> selected_handle() const {
    return selected_handle_seen_.load()
               ? std::make_optional(selected_handle_.load())
               : std::nullopt;
  }

 private:
  std::optional<std::string> Reply(uint32_t ordinal, const std::string& body) {
    base::DictValue arguments;
    switch (ordinal) {
      case 1u:
        arguments.Set("address", navigation_url_);
        return StreamedToolReply("errand-navigate", "browser.navigate",
                                 std::move(arguments));
      case 2u:
        arguments.Set("reason", "needs_a_person");
        return StreamedToolReply("errand-handover", "user.handover",
                                 std::move(arguments));
      case 3u: {
        const auto handle = FindErrandDownloadLinkHandleForTesting(body);
        if (!handle) {
          return std::nullopt;
        }
        selected_handle_.store(*handle);
        selected_handle_seen_.store(true);
        arguments.Set("node", static_cast<int>(*handle));
        return StreamedToolReply("errand-download",
                                 "browser.download.from_link",
                                 std::move(arguments));
      }
      case 4u:
        return StreamedToolReply("errand-list", "browser.download.list",
                                 std::move(arguments));
      default: {
        const auto complete = LatestErrandDownloadCompletionForTesting(body);
        if (!complete) {
          return std::nullopt;
        }
        if (*complete) {
          final_reply_sent_.store(true);
          return StreamedFinalReply("The PDF is ready. You can open the file.");
        }
        if (ordinal < kMaxRequests) {
          return StreamedToolReply("errand-list-" + base::NumberToString(ordinal),
                                   "browser.download.list", std::move(arguments));
        }
        return std::nullopt;
      }
    }
  }

  void Refuse(content::URLLoaderInterceptor::RequestParams* params,
              uint32_t ordinal,
              const char* reason) {
    invalid_request_seen_.store(true);
    ADD_FAILURE() << "Errand model fixture refused request " << ordinal << ": "
                  << reason;
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 500 Internal Server Error\nContent-Type: application/json\n",
        "{\"error\":\"errand fixture request refused\"}", params->client.get(),
        std::nullopt, GURL(kRequestUrl));
  }

  const std::string navigation_url_;
  std::atomic<uint32_t> request_count_{0u};
  std::atomic<bool> invalid_request_seen_{false};
  std::atomic<bool> private_input_seen_{false};
  std::atomic<uint32_t> selected_handle_{0u};
  std::atomic<bool> selected_handle_seen_{false};
  std::atomic<bool> final_reply_sent_{false};
  std::unique_ptr<content::URLLoaderInterceptor> interceptor_;
};

ErrandTaskModelEndpoint::ErrandTaskModelEndpoint(std::string navigation_url)
    : impl_(std::make_unique<Impl>(std::move(navigation_url))) {}
ErrandTaskModelEndpoint::~ErrandTaskModelEndpoint() = default;
std::string ErrandTaskModelEndpoint::base_url() const {
  return kBaseUrl;
}
uint32_t ErrandTaskModelEndpoint::request_count() const {
  return impl_->request_count();
}
bool ErrandTaskModelEndpoint::invalid_request_seen() const {
  return impl_->invalid_request_seen();
}
bool ErrandTaskModelEndpoint::private_input_seen() const {
  return impl_->private_input_seen();
}
std::optional<uint32_t> ErrandTaskModelEndpoint::selected_handle() const {
  return impl_->selected_handle();
}

}  // namespace taffy::test
