// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_throttle.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "net/base/net_errors.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/http_request_headers_update_params.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy::filtering {
namespace {

namespace proto = url_pattern_index::proto;

class CapturingDelegate : public blink::URLLoaderThrottle::Delegate {
 public:
  void CancelWithError(int error_code,
                       std::string_view custom_reason) override {
    cancelled_with = error_code;
    reason = std::string(custom_reason);
  }
  void Resume() override {}

  int cancelled_with = 0;
  std::string reason;
};

scoped_refptr<const SharedRuleset> RulesetOf(const char* list) {
  CompiledRuleset compiled = CompileRuleset(list);
  auto matcher = FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                              compiled.checksum);
  CHECK(matcher);
  return base::MakeRefCounted<SharedRuleset>(std::move(matcher));
}

network::ResourceRequest RequestFor(
    const char* url,
    network::mojom::RequestDestination destination) {
  network::ResourceRequest request;
  request.url = GURL(url);
  request.destination = destination;
  return request;
}

TEST(FilteringThrottleTest, ABlockedRequestIsCancelledAndCounted) {
  int blocked = 0;
  FilteringThrottle throttle(
      RulesetOf("||ads.example^\n"),
      url::Origin::Create(GURL("https://news.example")), false,
      base::BindRepeating([](int* blocked) { ++*blocked; }, &blocked));
  CapturingDelegate delegate;
  throttle.set_delegate(&delegate);
  auto request = RequestFor("https://ads.example/banner.png",
                            network::mojom::RequestDestination::kImage);
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, delegate.cancelled_with);
  EXPECT_EQ("TaffyFiltering", delegate.reason);
  EXPECT_EQ(1, blocked);
  EXPECT_FALSE(defer);
}

TEST(FilteringThrottleTest, AnUnmatchedRequestPassesUntouched) {
  int blocked = 0;
  FilteringThrottle throttle(
      RulesetOf("||ads.example^\n"),
      url::Origin::Create(GURL("https://news.example")), false,
      base::BindRepeating([](int* blocked) { ++*blocked; }, &blocked));
  CapturingDelegate delegate;
  throttle.set_delegate(&delegate);
  auto request = RequestFor("https://plain.example/app.js",
                            network::mojom::RequestDestination::kScript);
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);
  EXPECT_EQ(0, delegate.cancelled_with);
  EXPECT_EQ(0, blocked);
}

TEST(FilteringThrottleTest, ARedirectIntoABlockedHostIsCaught) {
  int blocked = 0;
  FilteringThrottle throttle(
      RulesetOf("||track.example^\n"),
      url::Origin::Create(GURL("https://news.example")), false,
      base::BindRepeating([](int* blocked) { ++*blocked; }, &blocked));
  CapturingDelegate delegate;
  throttle.set_delegate(&delegate);
  auto request = RequestFor("https://clean.example/pixel",
                            network::mojom::RequestDestination::kImage);
  bool defer = false;
  throttle.WillStartRequest(&request, &defer);
  ASSERT_EQ(0, delegate.cancelled_with);

  net::RedirectInfo redirect;
  redirect.new_url = GURL("https://track.example/pixel");
  auto head = network::mojom::URLResponseHead::New();
  network::HttpRequestHeadersUpdateParams headers_update_params;
  throttle.WillRedirectRequest(&redirect, *head, &defer,
                               &headers_update_params);
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT, delegate.cancelled_with);
  EXPECT_EQ(1, blocked);
}

TEST(FilteringThrottleTest, DocumentsAndNonWebSchemesAreNeverJudged) {
  int blocked = 0;
  FilteringThrottle throttle(
      RulesetOf("||ads.example^\n"),
      url::Origin::Create(GURL("https://news.example")), false,
      base::BindRepeating([](int* blocked) { ++*blocked; }, &blocked));
  CapturingDelegate delegate;
  throttle.set_delegate(&delegate);
  bool defer = false;

  auto document = RequestFor("https://ads.example/",
                             network::mojom::RequestDestination::kDocument);
  throttle.WillStartRequest(&document, &defer);
  EXPECT_EQ(0, delegate.cancelled_with);

  auto data = RequestFor("data:image/png;base64,AAAA",
                         network::mojom::RequestDestination::kImage);
  throttle.WillStartRequest(&data, &defer);
  EXPECT_EQ(0, delegate.cancelled_with);
  EXPECT_EQ(0, blocked);
}

TEST(FilteringThrottleTest, TheDestinationMapCoversTheRuleVocabulary) {
  using Destination = network::mojom::RequestDestination;
  EXPECT_EQ(proto::ELEMENT_TYPE_SUBDOCUMENT,
            ElementTypeForDestination(Destination::kIframe));
  EXPECT_EQ(proto::ELEMENT_TYPE_SCRIPT,
            ElementTypeForDestination(Destination::kScript));
  EXPECT_EQ(proto::ELEMENT_TYPE_SCRIPT,
            ElementTypeForDestination(Destination::kServiceWorker));
  EXPECT_EQ(proto::ELEMENT_TYPE_IMAGE,
            ElementTypeForDestination(Destination::kImage));
  EXPECT_EQ(proto::ELEMENT_TYPE_STYLESHEET,
            ElementTypeForDestination(Destination::kStyle));
  EXPECT_EQ(proto::ELEMENT_TYPE_FONT,
            ElementTypeForDestination(Destination::kFont));
  EXPECT_EQ(proto::ELEMENT_TYPE_MEDIA,
            ElementTypeForDestination(Destination::kVideo));
  EXPECT_EQ(proto::ELEMENT_TYPE_OBJECT,
            ElementTypeForDestination(Destination::kEmbed));
  EXPECT_EQ(proto::ELEMENT_TYPE_XMLHTTPREQUEST,
            ElementTypeForDestination(Destination::kEmpty));
}

}  // namespace
}  // namespace taffy::filtering
