// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/comparison_vertical_model_endpoint.h"

#include <atomic>
#include <string_view>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "content/public/test/url_loader_interceptor.h"
#include "services/network/test/test_utils.h"
#include "taffy/test/recovery/task_model_reply_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

constexpr char kBaseUrl[] = "https://comparison-model.test/v1";
constexpr char kRequestUrl[] =
    "https://comparison-model.test/v1/chat/completions";

}  // namespace

bool HasBothComparisonPagesForTesting(const std::string& request_body) {
  const auto parsed = base::JSONReader::ReadDict(request_body, base::JSON_PARSE_RFC);
  const auto* messages = parsed ? parsed->FindList("messages") : nullptr;
  if (!messages) {
    return false;
  }
  for (const auto& message : *messages) {
    const auto* dict = message.GetIfDict();
    const auto* role = dict ? dict->FindString("role") : nullptr;
    const auto* text = dict ? dict->FindString("content") : nullptr;
    if (!role || *role != "user" || !text) {
      continue;
    }
    bool complete = true;
    for (std::string_view token : {"Cedar Phone at Orchard", "$349.00",
                                   "Cedar Phone at Harbor", "$329.00"}) {
      complete &= text->find(token) != std::string::npos;
    }
    if (complete) {
      return true;
    }
  }
  return false;
}

class ComparisonVerticalModelEndpoint::Impl final {
 public:
  Impl()
      : interceptor_(std::make_unique<content::URLLoaderInterceptor>(
            base::BindRepeating(&Impl::Intercept, base::Unretained(this)))) {}

  bool Intercept(content::URLLoaderInterceptor::RequestParams* params) {
    if (params->url_request.url != GURL(kRequestUrl)) {
      return false;
    }
    const auto& request = params->url_request;
    const uint32_t ordinal = request_count_.fetch_add(1u) + 1u;
    const std::string body = request.request_body ? network::GetUploadData(request)
                                                 : std::string();
    const auto parsed = base::JSONReader::ReadDict(body, base::JSON_PARSE_RFC);
    const bool both_pages = HasBothComparisonPagesForTesting(body);
    both_pages_seen_.store(both_pages);
    const bool valid = ordinal == 1u && request.method == "POST" &&
                       request.request_body &&
                       request.headers.GetHeader("Content-Type") == "application/json" &&
                       !request.headers.HasHeader("Authorization") && parsed &&
                       parsed->FindBool("stream").value_or(false) && both_pages;
    if (!valid) {
      invalid_request_seen_.store(true);
      ADD_FAILURE() << "Comparison model fixture received an early, incomplete, "
                       "or additional request: " << ordinal;
      content::URLLoaderInterceptor::WriteResponse(
          "HTTP/1.1 500 Internal Server Error\nContent-Type: application/json\n",
          "{\"error\":\"comparison fixture request refused\"}",
          params->client.get(), std::nullopt, GURL(kRequestUrl));
      return true;
    }
    content::URLLoaderInterceptor::WriteResponse(
        "HTTP/1.1 200 OK\nContent-Type: text/event-stream\n",
        StreamedFinalReply("Harbor lists Cedar Phone for $329.00, $20.00 less "
                           "than Orchard at $349.00."),
        params->client.get(), std::nullopt, GURL(kRequestUrl));
    return true;
  }

  uint32_t request_count() const { return request_count_.load(); }
  bool invalid_request_seen() const { return invalid_request_seen_.load(); }
  bool both_pages_seen() const { return both_pages_seen_.load(); }

 private:
  std::atomic<uint32_t> request_count_{0u};
  std::atomic<bool> invalid_request_seen_{false};
  std::atomic<bool> both_pages_seen_{false};
  std::unique_ptr<content::URLLoaderInterceptor> interceptor_;
};

ComparisonVerticalModelEndpoint::ComparisonVerticalModelEndpoint()
    : impl_(std::make_unique<Impl>()) {}
ComparisonVerticalModelEndpoint::~ComparisonVerticalModelEndpoint() = default;
std::string ComparisonVerticalModelEndpoint::base_url() const { return kBaseUrl; }
uint32_t ComparisonVerticalModelEndpoint::request_count() const {
  return impl_->request_count();
}
bool ComparisonVerticalModelEndpoint::invalid_request_seen() const {
  return impl_->invalid_request_seen();
}
bool ComparisonVerticalModelEndpoint::both_pages_seen() const {
  return impl_->both_pages_seen();
}

}  // namespace taffy::test
