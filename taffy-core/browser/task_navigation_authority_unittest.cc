// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_navigation_authority.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "content/public/browser/navigation_ui_data.h"
#include "content/public/test/mock_navigation_handle.h"
#include "taffy/browser/task_navigation_authority_platform.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

class FakeNavigationUIData final : public content::NavigationUIData {
 public:
  explicit FakeNavigationUIData(std::string destination)
      : destination_(std::move(destination)) {}

  std::unique_ptr<content::NavigationUIData> Clone() override {
    return std::make_unique<FakeNavigationUIData>(destination_);
  }

  const std::string& destination() const { return destination_; }

 private:
  const std::string destination_;
};

class FakeTaskNavigationAuthorityPlatform final
    : public TaskNavigationAuthorityPlatform {
 public:
  std::unique_ptr<content::NavigationUIData> CreateNavigationData(
      std::string exact_destination) const override {
    return std::make_unique<FakeNavigationUIData>(std::move(exact_destination));
  }

  std::optional<std::string> ReadExactDestination(
      const content::NavigationUIData& navigation_data) const override {
    return static_cast<const FakeNavigationUIData&>(navigation_data)
        .destination();
  }
};

class TaskNavigationAuthorityTest : public testing::Test {
 protected:
  FakeTaskNavigationAuthorityPlatform platform_;
  ScopedTaskNavigationAuthorityPlatformForTesting install_{&platform_};
};

// The request goes exactly where it was told, and a redirect is the site's
// answer to it (decision 0177).
TEST_F(TaskNavigationAuthorityTest, OnlyTheExactAddressIsRequestedDirectly) {
  const GURL destination("https://shop.example.com/item?id=7#details");
  std::unique_ptr<content::NavigationUIData> prepared =
      TaskNavigationAuthority::CreateNavigationData(destination);
  ASSERT_TRUE(prepared);
  std::unique_ptr<content::NavigationUIData> request_data = prepared->Clone();
  ASSERT_TRUE(request_data);

  content::MockNavigationHandle handle;
  EXPECT_CALL(handle, GetNavigationUIData())
      .WillOnce(testing::Return(request_data.get()));

  std::optional<TaskNavigationAuthority> authority =
      TaskNavigationAuthority::FromNavigation(handle);
  ASSERT_TRUE(authority);
  // The address that was asked for, whichever way it is reached.
  EXPECT_TRUE(authority->AllowsRequest(destination, /*is_redirect=*/false));
  EXPECT_TRUE(authority->AllowsRequest(destination, /*is_redirect=*/true));
  // Anything else, requested rather than redirected to, is a hop nobody asked
  // for.
  EXPECT_FALSE(authority->AllowsRequest(
      GURL("https://shop.example.com/another"), /*is_redirect=*/false));
  EXPECT_FALSE(authority->AllowsRequest(GURL("https://elsewhere.example.com/"),
                                        /*is_redirect=*/false));
}

// Where a site may send the request it was given, which is the rule the
// postcondition beside it now agrees with (decisions 0177, 0179).
TEST_F(TaskNavigationAuthorityTest, ASiteMayAnswerTheRequestElsewhere) {
  const GURL destination("https://shop.example.com/item");
  std::unique_ptr<content::NavigationUIData> prepared =
      TaskNavigationAuthority::CreateNavigationData(destination);
  ASSERT_TRUE(prepared);
  std::unique_ptr<content::NavigationUIData> request_data = prepared->Clone();
  ASSERT_TRUE(request_data);

  content::MockNavigationHandle handle;
  EXPECT_CALL(handle, GetNavigationUIData())
      .WillOnce(testing::Return(request_data.get()));

  std::optional<TaskNavigationAuthority> authority =
      TaskNavigationAuthority::FromNavigation(handle);
  ASSERT_TRUE(authority);
  // Its own registrable domain: an apex, a www, a regional host.
  EXPECT_TRUE(authority->AllowsRequest(GURL("https://www.example.com/item"),
                                       /*is_redirect=*/true));
  // A cross-site hop the class table does not refuse — which is how a search
  // engine's result link reaches the site it names.
  EXPECT_TRUE(authority->AllowsRequest(GURL("https://official.example.org/"),
                                       /*is_redirect=*/true));
  // A cross-site hop it does refuse.
  EXPECT_FALSE(authority->AllowsRequest(GURL("https://mail.google.com/"),
                                        /*is_redirect=*/true));
  // And never off https.
  EXPECT_FALSE(authority->AllowsRequest(GURL("http://www.example.com/item"),
                                        /*is_redirect=*/true));

  // Each of those answers has a clause behind it, and the log prints the
  // clause rather than the address. One `at=not-the-authorized-destination`
  // stood for all six of these and a phone spent ten seconds on each without
  // saying which (decision 0226).
  using Verdict = TaskNavigationAuthority::RequestVerdict;
  EXPECT_EQ(authority->VerdictForRequest(GURL("https://www.example.com/item"),
                                         /*is_redirect=*/true),
            Verdict::kAllowedSameSiteHop);
  EXPECT_EQ(authority->VerdictForRequest(GURL("https://official.example.org/"),
                                         /*is_redirect=*/true),
            Verdict::kAllowedCrossSiteHop);
  EXPECT_EQ(authority->VerdictForRequest(GURL("https://mail.google.com/"),
                                         /*is_redirect=*/true),
            Verdict::kRefusedRestrictedClass);
  EXPECT_EQ(authority->VerdictForRequest(GURL("http://www.example.com/item"),
                                         /*is_redirect=*/true),
            Verdict::kRefusedInsecureHop);
  EXPECT_EQ(authority->VerdictForRequest(GURL("https://elsewhere.example.org/"),
                                         /*is_redirect=*/false),
            Verdict::kRefusedFirstRequestNotExact);
  EXPECT_EQ(authority->VerdictForRequest(GURL("ftp://example.com/"),
                                         /*is_redirect=*/true),
            Verdict::kRefusedNotNormalized);
  EXPECT_STREQ(TaskNavigationAuthority::NameOfVerdict(
                   Verdict::kRefusedRestrictedClass),
               "landing-in-restricted-class");
}

TEST_F(TaskNavigationAuthorityTest,
       InvalidAndCredentialBearingDestinationsNeverAcquireAuthority) {
  EXPECT_FALSE(
      TaskNavigationAuthority::CreateNavigationData(GURL("not a url")));
  EXPECT_FALSE(TaskNavigationAuthority::CreateNavigationData(
      GURL("https://user:password@example.test/")));
  EXPECT_FALSE(TaskNavigationAuthority::CreateNavigationData(
      GURL("file:///tmp/not-authorized")));
}

TEST_F(TaskNavigationAuthorityTest, AHandleWithoutPreStartDataHasNoAuthority) {
  content::MockNavigationHandle handle;
  EXPECT_CALL(handle, GetNavigationUIData()).WillOnce(testing::Return(nullptr));
  EXPECT_FALSE(TaskNavigationAuthority::FromNavigation(handle));
}

TEST(TaskNavigationAuthorityNoPlatformTest, MissingCarrierFailsClosed) {
  EXPECT_FALSE(TaskNavigationAuthority::CreateNavigationData(
      GURL("https://destination.example/exact")));
}

}  // namespace
}  // namespace taffy
