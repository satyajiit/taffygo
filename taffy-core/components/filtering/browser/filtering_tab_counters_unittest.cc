// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_tab_counters.h"

#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "components/prefs/testing_pref_service.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::filtering {
namespace {

class FilteringTabCountersTest : public content::RenderViewHostTestHarness {
 protected:
  FilteringTabCountersTest()
      : content::RenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  FilteringTabCounters* Counters() {
    FilteringTabCounters::CreateForWebContents(web_contents(), nullptr);
    return FilteringTabCounters::FromWebContents(web_contents());
  }
};

TEST_F(FilteringTabCountersTest, TheFirstBlockPublishesImmediately) {
  auto* counters = Counters();
  int publications = 0;
  counters->SetCountChangedCallback(base::BindRepeating(
      [](int* publications) { ++*publications; }, &publications));
  counters->NoteBlocked();
  EXPECT_EQ(1, publications);
  EXPECT_EQ(1u, counters->blocked_count());
}

TEST_F(FilteringTabCountersTest, LaterBlocksCoalesceToOnePublication) {
  auto* counters = Counters();
  int publications = 0;
  counters->SetCountChangedCallback(base::BindRepeating(
      [](int* publications) { ++*publications; }, &publications));
  counters->NoteBlocked();  // Immediate.
  counters->NoteBlocked();
  counters->NoteBlocked();
  counters->NoteBlocked();
  EXPECT_EQ(1, publications);
  EXPECT_EQ(4u, counters->blocked_count());
  task_environment()->FastForwardBy(base::Milliseconds(500));
  EXPECT_EQ(2, publications);
}

TEST_F(FilteringTabCountersTest, AFlushPublishesWhatTheTimerWasHolding) {
  auto* counters = Counters();
  int publications = 0;
  counters->SetCountChangedCallback(base::BindRepeating(
      [](int* publications) { ++*publications; }, &publications));
  counters->NoteBlocked();
  counters->NoteBlocked();
  EXPECT_EQ(1, publications);
  counters->FlushNow();
  EXPECT_EQ(2, publications);
}

TEST_F(FilteringTabCountersTest, APublicationReachesTheProfileWideSignal) {
  TestingPrefServiceSimple prefs;
  RegisterFilteringPreferences(prefs.registry());
  FilteringRulesetService service(
      &prefs,
      base::BindRepeating(
          [](base::OnceCallback<void(std::optional<std::string>)> reply) {
            std::move(reply).Run(std::nullopt);
          }));
  int changes = 0;
  auto subscription = service.AddChangedCallback(
      base::BindRepeating([](int* changes) { ++*changes; }, &changes));
  ASSERT_TRUE(subscription);
  FilteringTabCounters::CreateForWebContents(web_contents(),
                                             service.GetWeakPtr());
  FilteringTabCounters::FromWebContents(web_contents())->NoteBlocked();
  // The tab's immediate first publication rode the service's one signal, so
  // a projection holding the profile refreshes without a per-tab
  // subscription.
  EXPECT_EQ(1, changes);
  EXPECT_EQ(1, service.blocked_total());
}

TEST_F(FilteringTabCountersTest, ANonPrivateBlockCountsInTheWeekWindow) {
  TestingPrefServiceSimple prefs;
  RegisterFilteringPreferences(prefs.registry());
  FilteringRulesetService service(
      &prefs,
      base::BindRepeating(
          [](base::OnceCallback<void(std::optional<std::string>)> reply) {
            std::move(reply).Run(std::nullopt);
          }));
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://news.example/"));
  FilteringTabCounters::CreateForWebContents(web_contents(),
                                             service.GetWeakPtr());
  FilteringTabCounters::FromWebContents(web_contents())->NoteBlocked();
  EXPECT_EQ(1, service.blocked_this_week());
  EXPECT_EQ(1, service.minimum_sites_this_week());
}

TEST_F(FilteringTabCountersTest, ANewPageIsANewCountPublishedAtOnce) {
  auto* counters = Counters();
  counters->NoteBlocked();
  counters->NoteBlocked();
  EXPECT_EQ(2u, counters->blocked_count());
  int publications = 0;
  counters->SetCountChangedCallback(base::BindRepeating(
      [](int* publications) { ++*publications; }, &publications));
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("https://elsewhere.example/"));
  EXPECT_EQ(0u, counters->blocked_count());
  EXPECT_GE(publications, 1);
}

}  // namespace
}  // namespace taffy::filtering
