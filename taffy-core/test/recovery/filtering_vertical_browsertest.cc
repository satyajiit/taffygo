// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/hash.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "taffy/browser/assets/bundled_asset_seeder.h"
#include "taffy/browser/assets/profile_asset_plane.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"
#include "taffy/components/filtering/browser/filtering_tab_counters.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/zlib/google/zip.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;
namespace proto = url_pattern_index::proto;

constexpr char kAssetId[] = "easylist-base";
constexpr char kAssetRevision[] = "zz-filtering-vertical-1";
// Where the package would carry it, in the form `OpenApkAsset` takes. The
// real rows are generated into taffy-core/browser/assets/generated/; this
// one is the shape of a row rather than one of them, because a pack with a
// rule this test can assert on is not a pack the product ships.
constexpr char kPackagedPath[] = "assets/taffy-delivery/easylist-base.zip";
constexpr char kDocumentHost[] = "page.filtering.test";
constexpr char kBlockedHost[] = "ads.filtering.test";
constexpr char kPrivateHost[] = "private.filtering.test";

std::string AppendIframeScript(const GURL& url) {
  return content::JsReplace(
      R"((() => {
        const frame = document.createElement('iframe');
        frame.src = $1;
        document.body.appendChild(frame);
        return true;
      })())",
      url);
}

std::string FetchSucceededScript(const GURL& url) {
  return content::JsReplace(
      R"((async () => {
        try {
          await fetch($1, {mode: 'no-cors', cache: 'no-store'});
          return true;
        } catch (error) {
          return false;
        }
      })())",
      url);
}

class FilteringVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    PlatformBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");

    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(pack_directory_.CreateUniqueTempDir());
      const base::FilePath root =
          pack_directory_.GetPath().AppendASCII("filter-pack");
      const base::FilePath lists = root.AppendASCII("lists");
      package_root_ = pack_directory_.GetPath().AppendASCII("package");
      const base::FilePath packaged = package_root_.AppendASCII("assets")
                                          .AppendASCII("taffy-delivery");
      const base::FilePath archive =
          packaged.AppendASCII("easylist-base.zip");
      ASSERT_TRUE(base::CreateDirectory(lists));
      ASSERT_TRUE(base::CreateDirectory(packaged));
      ASSERT_TRUE(base::WriteFile(
          lists.AppendASCII("easylist.txt"),
          "[Adblock Plus 2.0]\n||ads.filtering.test^\n"));
      ASSERT_TRUE(base::WriteFile(lists.AppendASCII("easyprivacy.txt"),
                                  "! deterministic empty companion list\n"));
      ASSERT_TRUE(zip::Zip(root, archive, /*include_hidden_files=*/true));
      ASSERT_TRUE(base::ReadFileToString(archive, &pack_bytes_));
    }
    ASSERT_FALSE(pack_bytes_.empty());

    std::array<uint8_t, 32> digest = {};
    crypto::hash::Hash(crypto::hash::kSha256, base::as_byte_span(pack_bytes_),
                       digest);
    pack_digest_ = base::ToLowerASCII(base::HexEncode(digest));

    embedded_test_server()->RegisterRequestMonitor(base::BindRepeating(
        &FilteringVerticalBrowserTest::ObserveServerRequest,
        base::Unretained(this)));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    if (embedded_test_server()->Started()) {
      EXPECT_TRUE(embedded_test_server()->ShutdownAndWaitUntilComplete());
    }
    PlatformBrowserTest::TearDownOnMainThread();
  }

  content::WebContents* active_tab() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  void ObserveServerRequest(const net::test_server::HttpRequest& request) {
    // EmbeddedTestServer normalizes GetURL() against its loopback base URL, so
    // the request target is the stable way to recognize these cross-host
    // probes regardless of the Host header spelling on Android.
    if (request.relative_url.find("attempt=") != std::string::npos) {
      blocked_origin_requests_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // The row the packaged artifact would have. Its views point at members of
  // this fixture, which is what makes waiting on the seed mandatory rather
  // than tidy.
  BundledAssetRow PackagedFilterPack() const {
    return BundledAssetRow{kAssetId, kAssetRevision, kPackagedPath,
                           pack_bytes_.size(), pack_digest_};
  }

  base::ScopedTempDir pack_directory_;
  base::FilePath package_root_;
  std::string pack_bytes_;
  std::string pack_digest_;
  std::atomic<int> blocked_origin_requests_{0};
};

IN_PROC_BROWSER_TEST_F(
    FilteringVerticalBrowserTest,
    InstalledRulesBlockAndSiteExceptionPublishesTheChangedPosture) {
  content::WebContents* const tab = active_tab();
  ASSERT_TRUE(tab);
  Profile* const profile =
      Profile::FromBrowserContext(tab->GetBrowserContext());
  ASSERT_TRUE(profile);
  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  ASSERT_TRUE(manager);
  filtering::FilteringRulesetService* const filtering =
      manager->filtering_service();
  ASSERT_TRUE(filtering);

  // Enter the way a first run does: the package carries the pack and the plane
  // installs it before it answers the scan (decision 0202). Only where the
  // bytes come from is the test's — the digest check, the profile store, the
  // atomic commit, the installed-set announcement, the pack reader, the
  // compiler and the ruleset swap are all shipping code, and the
  // announcement is what makes the filtering service reload without this test
  // asking it to.
  base::test::TestFuture<std::vector<service::AssetOnDiskPtr>> scanned;
  manager->asset_plane().SeedBundledPartsAndScanForTesting(
      package_root_, {PackagedFilterPack()}, scanned.GetCallback());
  const std::vector<service::AssetOnDiskPtr> on_disk = scanned.Take();
  bool installed = false;
  for (const service::AssetOnDiskPtr& row : on_disk) {
    installed = installed || (row->asset_id == kAssetId &&
                              row->asset_revision == kAssetRevision &&
                              row->presence == service::AssetPresence::kInstalled);
  }
  ASSERT_TRUE(installed) << "the packaged filter pack was not installed";

  const GURL document_url =
      embedded_test_server()->GetURL(kDocumentHost, "/title1.html");
  const GURL blocked_url = embedded_test_server()->GetURL(
      kBlockedHost, "/title1.html?attempt=blocked");
  const url::Origin document_origin = url::Origin::Create(document_url);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    scoped_refptr<const filtering::SharedRuleset> ruleset =
        filtering->ruleset();
    return ruleset && ruleset->matcher().ShouldBlockRequest(
                          blocked_url, document_origin,
                          proto::ELEMENT_TYPE_SUBDOCUMENT,
                          /*disable_generic_rules=*/false);
  })) << "the installed filter pack never became the active matcher";

  ASSERT_TRUE(content::NavigateToURL(tab, document_url));
  ASSERT_TRUE(filtering->posture().enabled());
  ASSERT_TRUE(filtering->posture().ActiveForHost(kDocumentHost));
  ASSERT_EQ(0, filtering->blocked_total());
  ASSERT_FALSE(filtering->has_week_window());

  int publications = 0;
  base::CallbackListSubscription subscription = filtering->AddChangedCallback(
      base::BindRepeating([](int* count) { ++*count; }, &publications));

  ASSERT_EQ(true, content::EvalJs(tab, AppendIframeScript(blocked_url)));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    filtering::FilteringTabCounters* counters =
        filtering::FilteringTabCounters::FromWebContents(tab);
    return counters && counters->blocked_count() == 1u &&
           filtering->blocked_total() == 1;
  }));
  filtering::FilteringTabCounters* const counters =
      filtering::FilteringTabCounters::FromWebContents(tab);
  ASSERT_TRUE(counters);
  EXPECT_EQ(1u, counters->blocked_count());
  EXPECT_EQ(1, filtering->blocked_total());
  EXPECT_TRUE(filtering->has_week_window());
  EXPECT_EQ(1, filtering->blocked_this_week());
  EXPECT_EQ(1, filtering->minimum_sites_this_week());
  EXPECT_EQ(0,
            blocked_origin_requests_.load(std::memory_order_relaxed));
  EXPECT_GE(publications, 1)
      << "the tab count must fan out through the profile publication";

  const uint64_t posture_before_exception = filtering->posture_revision();
  ASSERT_TRUE(filtering->SetSiteException(kDocumentHost, /*allow=*/true));
  ASSERT_GT(filtering->posture_revision(), posture_before_exception);
  ASSERT_FALSE(filtering->posture().ActiveForHost(kDocumentHost));

  const GURL allowed_url = embedded_test_server()->GetURL(
      kBlockedHost, "/title1.html?attempt=excepted");
  ASSERT_EQ(true, content::EvalJs(tab, AppendIframeScript(allowed_url)));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return blocked_origin_requests_.load(std::memory_order_relaxed) == 1;
  })) << "the site exception did not let the subframe reach the server";
  EXPECT_EQ(1u, counters->blocked_count());
  EXPECT_EQ(1, filtering->blocked_total());
  EXPECT_EQ(1, filtering->blocked_this_week());

  // Renderer-originated resources use their own Mojo-backed throttle. Turn
  // the exception back off and prove a fetch is refused before the server,
  // with the same browser-owned counters receiving the result.
  ASSERT_TRUE(filtering->SetSiteException(kDocumentHost, /*allow=*/false));
  ASSERT_TRUE(filtering->posture().ActiveForHost(kDocumentHost));
  const GURL blocked_fetch_url = embedded_test_server()->GetURL(
      kBlockedHost, "/title1.html?attempt=fetch-blocked");
  EXPECT_EQ(false,
            content::EvalJs(tab, FetchSucceededScript(blocked_fetch_url)));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return counters->blocked_count() == 2u &&
           filtering->blocked_total() == 2;
  })) << "the renderer resource did not publish its blocked result";
  EXPECT_EQ(1,
            blocked_origin_requests_.load(std::memory_order_relaxed));

  ASSERT_TRUE(filtering->SetSiteException(kDocumentHost, /*allow=*/true));
  const GURL allowed_fetch_url = embedded_test_server()->GetURL(
      kBlockedHost, "/title1.html?attempt=fetch-excepted");
  EXPECT_EQ(true,
            content::EvalJs(tab, FetchSucceededScript(allowed_fetch_url)));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return blocked_origin_requests_.load(std::memory_order_relaxed) == 2;
  })) << "the site exception did not admit the renderer resource";
  EXPECT_EQ(2u, counters->blocked_count());
  EXPECT_EQ(2, filtering->blocked_total());
  EXPECT_EQ(2, filtering->blocked_this_week());
}

// Decision 0128: a private tab's allowances belong to the private profile's
// own plane. Nothing on a host can prove that — the disagreement only exists
// between two real `Profile`s and their real `PrefService`s — so this is where
// it is proved.
IN_PROC_BROWSER_TEST_F(FilteringVerticalBrowserTest,
                       APrivateProfileKeepsItsOwnAllowancesAndWritesNoneOfThem) {
  content::WebContents* const tab = active_tab();
  ASSERT_TRUE(tab);
  Profile* const profile =
      Profile::FromBrowserContext(tab->GetBrowserContext());
  ASSERT_TRUE(profile);
  ASSERT_FALSE(profile->IsOffTheRecord());

  CoreServiceManager* const manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  ASSERT_TRUE(manager);
  filtering::FilteringRulesetService* const regular =
      manager->filtering_service();
  ASSERT_TRUE(regular);

  // A private window is still the product, so the factory installs the plane
  // on the off-the-record profile too. That it is a *different* service is the
  // fact everything below rests on.
  Profile* const otr = profile->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  ASSERT_TRUE(otr->IsOffTheRecord());
  CoreServiceManager* const otr_manager =
      CoreServiceManagerFactory::GetForProfile(otr);
  ASSERT_TRUE(otr_manager);
  filtering::FilteringRulesetService* const private_plane =
      otr_manager->filtering_service();
  ASSERT_TRUE(private_plane);
  ASSERT_NE(regular, private_plane);

  // Before any private edit, the overlay reads through: an allowance recorded
  // in the regular profile is visible privately. This is the state that made
  // the old single-plane projection look correct.
  ASSERT_TRUE(regular->SetSiteException(kDocumentHost, /*allow=*/true));
  EXPECT_TRUE(regular->posture().ExceptedForHost(kDocumentHost));
  EXPECT_TRUE(private_plane->posture().ExceptedForHost(kDocumentHost));

  // The private allowance. It reaches the private plane and nothing else.
  ASSERT_TRUE(private_plane->SetSiteException(kPrivateHost, /*allow=*/true));
  EXPECT_TRUE(private_plane->posture().ExceptedForHost(kPrivateHost));
  EXPECT_FALSE(regular->posture().ExceptedForHost(kPrivateHost));
  EXPECT_FALSE(regular->posture().ExceptedForHost("sub.private.filtering.test"));

  // And it is not on disk. The regular profile's own preference — the one
  // screen SCR-206 lists and the one a `run-as` read would find — names the
  // regular host and never the private one.
  const base::ListValue& recorded =
      profile->GetPrefs()->GetList(filtering::kFilteringSiteExceptionsPref);
  bool holds_regular_host = false;
  for (const base::Value& entry : recorded) {
    ASSERT_TRUE(entry.is_string());
    EXPECT_NE(kPrivateHost, entry.GetString())
        << "a private tab's allowance reached the regular profile's store";
    holds_regular_host |= entry.GetString() == kDocumentHost;
  }
  EXPECT_TRUE(holds_regular_host)
      << "the regular profile's own allowance was not recorded";

  // The consequence worth knowing rather than discovering: writing into the
  // overlay forks the list. A later regular-profile change is no longer
  // forwarded to the private session, and the fork dies with the profile.
  ASSERT_TRUE(regular->SetSiteException(kBlockedHost, /*allow=*/true));
  EXPECT_TRUE(regular->posture().ExceptedForHost(kBlockedHost));
  EXPECT_FALSE(private_plane->posture().ExceptedForHost(kBlockedHost));
}

}  // namespace
}  // namespace taffy
