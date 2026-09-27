// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_ruleset_service.h"

#include <optional>
#include <string>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::filtering {
namespace {

class FilteringRulesetServiceTest : public testing::Test {
 protected:
  FilteringRulesetServiceTest() {
    RegisterFilteringPreferences(prefs_.registry());
  }

  FilteringRulesetService::ListReader ReaderReturning(
      std::optional<std::string> text) {
    return base::BindRepeating(
        [](std::optional<std::string> text,
           base::OnceCallback<void(std::optional<std::string>)> reply) {
          std::move(reply).Run(text);
        },
        std::move(text));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TestingPrefServiceSimple prefs_;
};

TEST_F(FilteringRulesetServiceTest, ACompiledListBecomesTheMatcher) {
  FilteringRulesetService service(&prefs_, ReaderReturning("||ads.example^\n"));
  EXPECT_FALSE(service.ruleset());
  task_environment_.RunUntilIdle();
  EXPECT_TRUE(service.ruleset());
}

TEST_F(FilteringRulesetServiceTest, AnAbsentAssetLeavesNoMatcherStanding) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  task_environment_.RunUntilIdle();
  EXPECT_FALSE(service.ruleset());
}

TEST_F(FilteringRulesetServiceTest, ThePostureReadsThePreferences) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  EXPECT_TRUE(service.posture().enabled());
  EXPECT_TRUE(service.posture().ActiveForHost("news.example"));

  service.SetFilteringEnabled(false);
  EXPECT_FALSE(service.posture().enabled());

  service.SetFilteringEnabled(true);
  ASSERT_TRUE(service.SetSiteException("news.example", true));
  EXPECT_FALSE(service.posture().ActiveForHost("news.example"));
  EXPECT_FALSE(service.posture().ActiveForHost("sub.news.example"));
  EXPECT_TRUE(service.posture().ActiveForHost("other.example"));

  ASSERT_TRUE(service.SetSiteException("news.example", false));
  EXPECT_TRUE(service.posture().ActiveForHost("news.example"));
}

TEST_F(FilteringRulesetServiceTest, AnExceptionSurvivesAServiceRestart) {
  // The exception is a person's own configuration and lives in the profile's
  // preferences, so a second service over the same store must read it back.
  // This is what makes the regular profile's list durable, and it is also the
  // control against which decision 0128's private plane is the exception: an
  // off-the-record profile's overlay is never written, so an exception
  // recorded there has nothing to read back from.
  {
    FilteringRulesetService first(&prefs_, ReaderReturning(std::nullopt));
    ASSERT_TRUE(first.SetSiteException("news.example", true));
    ASSERT_TRUE(first.posture().ExceptedForHost("news.example"));
  }

  FilteringRulesetService second(&prefs_, ReaderReturning(std::nullopt));
  EXPECT_TRUE(second.posture().ExceptedForHost("news.example"));
  EXPECT_TRUE(second.posture().ExceptedForHost("sub.news.example"));
  EXPECT_FALSE(second.posture().ActiveForHost("news.example"));
  EXPECT_TRUE(second.posture().ActiveForHost("other.example"));
}

TEST_F(FilteringRulesetServiceTest,
       AnExceptionIsRememberedWhileTheMasterToggleIsOff) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  ASSERT_TRUE(service.SetSiteException("news.example", true));
  service.SetFilteringEnabled(false);

  // Off for everything, and still allowed for this one. The sheet reads both
  // facts and says them in different words (decision 0128).
  EXPECT_FALSE(service.posture().ActiveForHost("news.example"));
  EXPECT_FALSE(service.posture().ActiveForHost("other.example"));
  EXPECT_TRUE(service.posture().ExceptedForHost("news.example"));
  EXPECT_FALSE(service.posture().ExceptedForHost("other.example"));
}

TEST_F(FilteringRulesetServiceTest,
       RepeatedRequestReadsShareOnePostureSnapshot) {
  FilteringRulesetService service(&prefs_, ReaderReturning("||ads.example^\n"));
  task_environment_.RunUntilIdle();

  const FilteringPosture* const first = &service.posture();
  EXPECT_EQ(first, &service.posture());
  EXPECT_TRUE(first->enabled());

  service.SetSiteException("news.example", true);
  EXPECT_EQ(first, &service.posture());
  EXPECT_FALSE(service.posture().ActiveForHost("news.example"));
}

TEST_F(FilteringRulesetServiceTest, AHostThisSeamRefusesIsNotRecorded) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  EXPECT_FALSE(service.SetSiteException("", true));
  EXPECT_FALSE(service.SetSiteException("https://news.example", true));
  EXPECT_FALSE(service.SetSiteException("news.example/path", true));
  EXPECT_FALSE(service.SetSiteException(std::string(300, 'a'), true));
  EXPECT_TRUE(prefs_.GetList(kFilteringSiteExceptionsPref).empty());
}

TEST_F(FilteringRulesetServiceTest, ThePostureChangeSignalsOnce) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  int changes = 0;
  auto subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &changes));
  ASSERT_TRUE(subscription);
  service.SetFilteringEnabled(false);
  EXPECT_EQ(1, changes);
  service.SetSiteException("news.example", true);
  EXPECT_EQ(2, changes);
}

TEST_F(FilteringRulesetServiceTest, ATabCountPublicationRidesTheOneSignal) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  int changes = 0;
  auto subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &changes));
  ASSERT_TRUE(subscription);
  service.NoteTabCountPublished();
  EXPECT_EQ(1, changes);
}

TEST_F(FilteringRulesetServiceTest, EveryLiveProjectionReceivesAChange) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  int first_changes = 0;
  int second_changes = 0;
  auto first_subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &first_changes));
  auto second_subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &second_changes));
  ASSERT_TRUE(first_subscription);
  ASSERT_TRUE(second_subscription);

  service.NoteTabCountPublished();

  EXPECT_EQ(1, first_changes);
  EXPECT_EQ(1, second_changes);
}

TEST_F(FilteringRulesetServiceTest,
       ClosingOneWindowProjectionDoesNotSilenceAnother) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  int first_changes = 0;
  int second_changes = 0;
  auto first_subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &first_changes));
  auto second_subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &second_changes));
  ASSERT_TRUE(first_subscription);
  ASSERT_TRUE(second_subscription);

  first_subscription = {};
  service.NoteTabCountPublished();

  EXPECT_EQ(0, first_changes);
  EXPECT_EQ(1, second_changes);
}

TEST_F(FilteringRulesetServiceTest,
       AProjectionMayUnsubscribeDuringNotification) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  int self_changes = 0;
  int surviving_changes = 0;
  base::CallbackListSubscription self_subscription;
  self_subscription = service.AddChangedCallback(base::BindRepeating(
      [](base::CallbackListSubscription* subscription, int* changes) {
        ++*changes;
        *subscription = {};
      },
      &self_subscription, &self_changes));
  auto surviving_subscription = service.AddChangedCallback(base::BindRepeating(
      [](int* changes) { ++*changes; }, &surviving_changes));
  ASSERT_TRUE(self_subscription);
  ASSERT_TRUE(surviving_subscription);

  service.NoteTabCountPublished();
  EXPECT_FALSE(self_subscription);
  EXPECT_EQ(1, self_changes);
  EXPECT_EQ(1, surviving_changes);

  service.NoteTabCountPublished();
  EXPECT_EQ(1, self_changes);
  EXPECT_EQ(2, surviving_changes);
}

TEST_F(FilteringRulesetServiceTest, ASubscriptionMayOutliveTheService) {
  base::CallbackListSubscription subscription;
  int changes = 0;
  {
    FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
    subscription = service.AddChangedCallback(
        base::BindRepeating([](int* changes) { ++*changes; }, &changes));
    ASSERT_TRUE(subscription);
  }

  EXPECT_EQ(0, changes);
  EXPECT_TRUE(subscription);
  subscription = {};
  EXPECT_FALSE(subscription);
}

TEST_F(FilteringRulesetServiceTest, TheTotalFlushesCoarselyAndAtTeardown) {
  {
    FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
    service.NoteBlocked();
    service.NoteBlocked();
    // Nothing written yet: the flush is at most every thirty seconds.
    EXPECT_EQ(0, prefs_.GetInt64(kFilteringBlockedTotalPref));
    EXPECT_EQ(2, service.blocked_total());
    task_environment_.FastForwardBy(base::Seconds(31));
    EXPECT_EQ(2, prefs_.GetInt64(kFilteringBlockedTotalPref));
    service.NoteBlocked();
  }
  // Teardown flushed the straggler.
  EXPECT_EQ(3, prefs_.GetInt64(kFilteringBlockedTotalPref));
}

TEST_F(FilteringRulesetServiceTest,
       TheWeekWindowCountsRequestsAndDistinctHosts) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  service.NoteWeekBlocked("news.example");
  service.NoteWeekBlocked("news.example");
  service.NoteWeekBlocked("docs.example");
  EXPECT_EQ(3, service.blocked_this_week());
  EXPECT_EQ(2, service.minimum_sites_this_week());
  // Lifetime is a different counter: a week note does not invent a lifetime.
  EXPECT_EQ(0, service.blocked_total());
}

TEST_F(FilteringRulesetServiceTest,
       DistinctWeekSitesFlushOnceInsteadOfWritingPerBlock) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  for (int index = 0; index < 100; ++index) {
    service.NoteWeekBlocked("site-" + std::to_string(index) + ".example");
  }

  EXPECT_EQ(100, service.minimum_sites_this_week());
  EXPECT_TRUE(prefs_.GetList(kFilteringWeekSitesPref).empty());

  task_environment_.FastForwardBy(base::Seconds(31));
  EXPECT_EQ(100u, prefs_.GetList(kFilteringWeekSitesPref).size());
}

TEST_F(FilteringRulesetServiceTest,
       HundredThousandSitesStayAtTheConservativeMemoryBound) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  for (int index = 0; index < 100'000; ++index) {
    service.NoteWeekBlocked("site-" + std::to_string(index) + ".example");
  }

  EXPECT_EQ(FilteringRulesetService::kWeekSiteLowerBoundCapacity,
            static_cast<size_t>(service.minimum_sites_this_week()));
  task_environment_.FastForwardBy(base::Seconds(31));
  EXPECT_EQ(FilteringRulesetService::kWeekSiteLowerBoundCapacity,
            prefs_.GetList(kFilteringWeekSitesPref).size());
}

TEST_F(FilteringRulesetServiceTest,
       OversizedLegacySiteListIsBoundedAcrossRestart) {
  prefs_.SetInt64(
      kFilteringWeekStartPref,
      base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds());
  base::ListValue legacy;
  for (size_t index = 0;
       index < FilteringRulesetService::kWeekSiteLowerBoundCapacity + 100;
       ++index) {
    legacy.Append("legacy-" + std::to_string(index) + ".example");
  }
  prefs_.SetList(kFilteringWeekSitesPref, std::move(legacy));

  {
    FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
    EXPECT_EQ(FilteringRulesetService::kWeekSiteLowerBoundCapacity,
              static_cast<size_t>(service.minimum_sites_this_week()));
  }
  EXPECT_EQ(FilteringRulesetService::kWeekSiteLowerBoundCapacity,
            prefs_.GetList(kFilteringWeekSitesPref).size());

  FilteringRulesetService restored(&prefs_, ReaderReturning(std::nullopt));
  EXPECT_EQ(FilteringRulesetService::kWeekSiteLowerBoundCapacity,
            static_cast<size_t>(restored.minimum_sites_this_week()));
}

TEST_F(FilteringRulesetServiceTest, TheWeekWindowResetsAfterSevenDays) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  service.NoteWeekBlocked("news.example");
  EXPECT_EQ(1, service.blocked_this_week());
  task_environment_.FastForwardBy(base::Days(7));
  EXPECT_EQ(0, service.blocked_this_week());
  EXPECT_EQ(0, service.minimum_sites_this_week());
  service.NoteWeekBlocked("elsewhere.example");
  EXPECT_EQ(1, service.blocked_this_week());
  EXPECT_EQ(1, service.minimum_sites_this_week());
}

TEST_F(FilteringRulesetServiceTest, ALifetimeBlockDoesNotInventAWeekCount) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  service.NoteBlocked();
  EXPECT_EQ(1, service.blocked_total());
  EXPECT_EQ(0, service.blocked_this_week());
  EXPECT_EQ(0, service.minimum_sites_this_week());
  EXPECT_FALSE(service.has_week_window());
  EXPECT_EQ(0, prefs_.GetInt64(kFilteringWeekStartPref));
}

TEST_F(FilteringRulesetServiceTest, ReadingTheWeekDoesNotMintAWindow) {
  FilteringRulesetService service(&prefs_, ReaderReturning(std::nullopt));
  EXPECT_FALSE(service.has_week_window());
  EXPECT_EQ(0, service.blocked_this_week());
  EXPECT_EQ(0, service.minimum_sites_this_week());
  EXPECT_FALSE(service.has_week_window());
  EXPECT_EQ(0, prefs_.GetInt64(kFilteringWeekStartPref));
}

}  // namespace
}  // namespace taffy::filtering
