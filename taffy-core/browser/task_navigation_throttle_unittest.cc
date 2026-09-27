// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/task_navigation_throttle.h"

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/test/mock_navigation_handle.h"
#include "taffy/browser/task_navigation_authority.h"
#include "taffy/browser/task_navigation_authority_platform.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/components/intelligence/content/task_navigation_refusal.h"
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

class FakeNavigationThrottleRegistry final
    : public content::NavigationThrottleRegistry {
 public:
  explicit FakeNavigationThrottleRegistry(
      content::NavigationHandle& navigation_handle)
      : navigation_handle_(navigation_handle) {}

  content::NavigationHandle& GetNavigationHandle() override {
    return *navigation_handle_;
  }

  void AddThrottle(
      std::unique_ptr<content::NavigationThrottle> throttle) override {
    throttles_.push_back(std::move(throttle));
  }

  bool HasThrottle(const std::string& name) override {
    for (const auto& throttle : throttles_) {
      if (name == throttle->GetNameForLogging()) {
        return true;
      }
    }
    return false;
  }

  bool EraseThrottleForTesting(const std::string& name) override {
    for (auto current = throttles_.begin(); current != throttles_.end();
         ++current) {
      if (name == (*current)->GetNameForLogging()) {
        throttles_.erase(current);
        return true;
      }
    }
    return false;
  }

  content::NavigationThrottle* only_throttle() {
    return throttles_.size() == 1u ? throttles_.front().get() : nullptr;
  }

 private:
  raw_ref<content::NavigationHandle> navigation_handle_;
  std::vector<std::unique_ptr<content::NavigationThrottle>> throttles_;
};

class TaskNavigationThrottleTest : public testing::Test {
 protected:
  FakeTaskNavigationAuthorityPlatform platform_;
  ScopedTaskNavigationAuthorityPlatformForTesting install_{&platform_};
};

std::unique_ptr<content::NavigationUIData> AuthorizeHandle(
    content::MockNavigationHandle& handle,
    const GURL& destination) {
  handle.set_url(destination);
  handle.set_is_in_primary_main_frame(true);
  handle.set_is_renderer_initiated(false);
  std::unique_ptr<content::NavigationUIData> data =
      TaskNavigationAuthority::CreateNavigationData(destination);
  EXPECT_TRUE(data);
  ON_CALL(handle, GetNavigationUIData())
      .WillByDefault(testing::Return(data.get()));
  return data;
}

TEST_F(TaskNavigationThrottleTest, ManualNavigationAllocatesNoThrottle) {
  content::MockNavigationHandle handle;
  handle.set_url(GURL("https://manual.example/"));
  handle.set_is_in_primary_main_frame(true);
  EXPECT_CALL(handle, GetNavigationUIData()).WillOnce(testing::Return(nullptr));
  FakeNavigationThrottleRegistry registry(handle);

  TaskNavigationThrottle::MaybeCreateAndAdd(registry);
  EXPECT_FALSE(registry.HasThrottle("TaskNavigationThrottle"));
}

// Decision 0177, at the throttle: the same rule its authority already states
// in `TaskNavigationAuthorityTest.ASiteMayAnswerTheRequestElsewhere`. A site
// may answer the request it was given somewhere else, which is how a result
// link reaches the site it names; what it may not do is send it to a
// destination class the table refuses, or off https.
TEST_F(TaskNavigationThrottleTest,
       AuthorizedInitialRequestProceedsAndASiteMayAnswerItElsewhere) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_EQ(throttle->WillStartRequest(), content::NavigationThrottle::PROCEED);

  handle.set_url(GURL("https://redirect.example/escape"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::PROCEED);
}

TEST_F(TaskNavigationThrottleTest, ARedirectTheClassTableRefusesIsStopped) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_EQ(throttle->WillStartRequest(), content::NavigationThrottle::PROCEED);

  handle.set_url(GURL("https://mail.google.com/"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
  // The verifier waiting on this navigation is told which refusal it was,
  // and the class table's has advice of its own (decision 0228).
  const TaskNavigationRefusal* refusal =
      TaskNavigationRefusal::GetForNavigationHandle(handle);
  ASSERT_TRUE(refusal);
  EXPECT_EQ(refusal->code(), ActionResultCode::kDestinationClassRestricted);
}

// Decision 0228. A site that redirects to its own http address is followed
// when Chromium upgrades the hop back onto https before sending it, which is
// what a host that has declared itself https-only gets.
TEST_F(TaskNavigationThrottleTest, AnHttpHopTheBrowserUpgradesIsFollowed) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_EQ(throttle->WillStartRequest(), content::NavigationThrottle::PROCEED);

  handle.set_url(GURL("http://destination.example/next"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::PROCEED);
  handle.set_url(GURL("https://destination.example/next"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::PROCEED);
  EXPECT_CALL(handle, IsDownload()).WillOnce(testing::Return(false));
  EXPECT_EQ(throttle->WillProcessResponse(),
            content::NavigationThrottle::PROCEED);
  EXPECT_FALSE(TaskNavigationRefusal::GetForNavigationHandle(handle));
}

// The same hop, not upgraded: the answer came over plain http, and a task
// never reads or acts on such a document. It is refused before anything
// commits, and the verifier is told it was a refusal.
TEST_F(TaskNavigationThrottleTest, AnHttpHopAnsweredOverHttpIsRefused) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_EQ(throttle->WillStartRequest(), content::NavigationThrottle::PROCEED);

  handle.set_url(GURL("http://destination.example/exact"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::PROCEED);
  EXPECT_EQ(throttle->WillProcessResponse(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
  const TaskNavigationRefusal* refusal =
      TaskNavigationRefusal::GetForNavigationHandle(handle);
  ASSERT_TRUE(refusal);
  EXPECT_EQ(refusal->code(), ActionResultCode::kEgressNotAuthorized);
}

// An http hop is judged as its https form would be, so one into a class the
// table refuses is still stopped where it stands.
TEST_F(TaskNavigationThrottleTest, AnHttpHopIntoARefusedClassIsStopped) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_EQ(throttle->WillStartRequest(), content::NavigationThrottle::PROCEED);

  handle.set_url(GURL("http://mail.google.com/"));
  EXPECT_EQ(throttle->WillRedirectRequest(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
  const TaskNavigationRefusal* refusal =
      TaskNavigationRefusal::GetForNavigationHandle(handle);
  ASSERT_TRUE(refusal);
  EXPECT_EQ(refusal->code(), ActionResultCode::kDestinationClassRestricted);
}

TEST_F(TaskNavigationThrottleTest,
       RewrittenInitialRequestAndRendererInitiationFailClosed) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle rewritten;
  std::unique_ptr<content::NavigationUIData> rewritten_data =
      AuthorizeHandle(rewritten, destination);
  ASSERT_TRUE(rewritten_data);
  rewritten.set_url(GURL("https://destination.example/changed"));
  FakeNavigationThrottleRegistry rewritten_registry(rewritten);
  TaskNavigationThrottle::MaybeCreateAndAdd(rewritten_registry);
  ASSERT_TRUE(rewritten_registry.only_throttle());
  EXPECT_EQ(rewritten_registry.only_throttle()->WillStartRequest(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
  const TaskNavigationRefusal* rewritten_refusal =
      TaskNavigationRefusal::GetForNavigationHandle(rewritten);
  ASSERT_TRUE(rewritten_refusal);
  EXPECT_EQ(rewritten_refusal->code(), ActionResultCode::kEgressNotAuthorized);

  content::MockNavigationHandle renderer;
  std::unique_ptr<content::NavigationUIData> renderer_data =
      AuthorizeHandle(renderer, destination);
  ASSERT_TRUE(renderer_data);
  renderer.set_is_renderer_initiated(true);
  FakeNavigationThrottleRegistry renderer_registry(renderer);
  TaskNavigationThrottle::MaybeCreateAndAdd(renderer_registry);
  ASSERT_TRUE(renderer_registry.only_throttle());
  EXPECT_EQ(renderer_registry.only_throttle()->WillStartRequest(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
}

TEST_F(TaskNavigationThrottleTest,
       AuthorizedNavigationRefusesAResponseThatBecomesADownload) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_CALL(handle, IsDownload()).WillOnce(testing::Return(true));
  EXPECT_EQ(throttle->WillProcessResponse(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
}

TEST_F(TaskNavigationThrottleTest,
       AuthorizedNavigationAllowsAnOrdinaryDocumentResponse) {
  const GURL destination("https://destination.example/exact");
  content::MockNavigationHandle handle;
  std::unique_ptr<content::NavigationUIData> data =
      AuthorizeHandle(handle, destination);
  ASSERT_TRUE(data);
  FakeNavigationThrottleRegistry registry(handle);
  TaskNavigationThrottle::MaybeCreateAndAdd(registry);

  content::NavigationThrottle* throttle = registry.only_throttle();
  ASSERT_TRUE(throttle);
  EXPECT_CALL(handle, IsDownload()).WillOnce(testing::Return(false));
  EXPECT_EQ(throttle->WillProcessResponse(),
            content::NavigationThrottle::PROCEED);
}

TEST_F(TaskNavigationThrottleTest,
       AssistantActionNavigationRefusesADownloadResponse) {
  content::MockNavigationHandle handle;
  FakeNavigationThrottleRegistry registry(handle);
  TaskActionDownloadThrottle throttle(registry);

  EXPECT_CALL(handle, IsDownload()).WillOnce(testing::Return(true));
  EXPECT_EQ(throttle.WillProcessResponse(),
            content::NavigationThrottle::CANCEL_AND_IGNORE);
}

TEST_F(TaskNavigationThrottleTest,
       AssistantActionNavigationAllowsAnOrdinaryResponse) {
  content::MockNavigationHandle handle;
  FakeNavigationThrottleRegistry registry(handle);
  TaskActionDownloadThrottle throttle(registry);

  EXPECT_CALL(handle, IsDownload()).WillOnce(testing::Return(false));
  EXPECT_EQ(throttle.WillProcessResponse(),
            content::NavigationThrottle::PROCEED);
}

}  // namespace
}  // namespace taffy
