// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_page_observation_broker_test_support.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

constexpr uint64_t kGeneration = 1u;

service::CoreBootstrapPtr Bootstrap(uint64_t generation, bool private_profile) {
  auto bootstrap = service::CoreBootstrap::New();
  bootstrap->service_generation = generation;
  bootstrap->private_profile = private_profile;
  bootstrap->browser_profile_id = "page-snapshot-browser-test-profile";
  return bootstrap;
}

#if BUILDFLAG(IS_ANDROID)
std::string ContentText(const api::PageSnapshotExportView& snapshot) {
  return std::string(snapshot.content.begin(), snapshot.content.end());
}
#endif

class PageSnapshotExportBrowserTest : public content::ContentBrowserTest {
 public:
  PageSnapshotExportBrowserTest()
      : origins_(test::FixtureOriginMap::Scheme::kHttps) {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    content::ContentBrowserTest::SetUpCommandLine(command_line);
    content::IsolateAllSitesForTesting(command_line);
  }

  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    origins_.Start();
    browser_context_ = web_contents()->GetBrowserContext();

    observation_broker_ = CorePageObservationBrokerTestPeer::Create(
        browser_context_,
        base::BindRepeating(&PageSnapshotExportBrowserTest::LookupManager,
                            base::Unretained(this)));

    CoreEffectBroker::Handlers handlers;
    handlers.load_bootstrap = base::BindRepeating(
        &PageSnapshotExportBrowserTest::LoadBootstrap, base::Unretained(this));
    handlers.commit_intent =
        base::BindRepeating([](const service::EffectEnvelope&,
                               CoreEffectBroker::JournalCallback callback) {
          std::move(callback).Run(true);
        });
    handlers.commit_result =
        base::BindRepeating([](const service::EffectResult&,
                               CoreEffectBroker::JournalCallback callback) {
          std::move(callback).Run(true);
        });
    handlers.observation = base::BindRepeating(
        [](scoped_refptr<CorePageObservationBroker> broker,
           service::EffectEnvelopePtr effect,
           CoreEffectBroker::CompletionCallback callback) {
          broker->Dispatch(std::move(effect), std::move(callback));
        },
        observation_broker_);
    handlers.cancel_generation = base::BindRepeating(
        [](scoped_refptr<CorePageObservationBroker> broker, uint64_t generation,
           std::vector<std::string>) { broker->CancelGeneration(generation); },
        observation_broker_);
    handlers.cancel_task = base::BindRepeating(
        [](scoped_refptr<CorePageObservationBroker> broker,
           std::string_view task_id,
           uint64_t generation) { broker->CancelTask(task_id, generation); },
        observation_broker_);

    auto tools = std::make_unique<ProfileToolSupervisor>(
        kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = quiet_tail_.MakeManager(
        browser_context_, /*storage_broker=*/nullptr, std::move(tools),
        observation_broker_,
        std::make_unique<CoreEffectBroker>(std::move(handlers)));
  }

  void TearDownOnMainThread() override {
    // The page host holds the manager's authority registries by address. Close
    // its WebContents before releasing the manager so no teardown callback can
    // observe a dangling authority seam.
    if (shell()) {
      shell()->Close();
    }
    if (manager_) {
      manager_->Shutdown();
    }
    manager_.reset();
    observation_broker_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  bool NavigateAndAttach(std::string_view fixture_id,
                         std::string_view suffix = {}) {
    const GURL target(origins_.FixtureUrl(fixture_id).spec() +
                      std::string(suffix));
    if (!content::NavigateToURL(shell(), target)) {
      return false;
    }
    TaffyPageIntelligenceHost::AttachWithAuthority(
        web_contents(), manager_->actor_leases(), manager_->capabilities(),
        manager_->value_references(), manager_.get(),
        manager_->task_journal_sink());
    return TaffyPageIntelligenceHost::FromWebContents(web_contents()) !=
           nullptr;
  }

  api::PageSnapshotExportResultPtr Export(std::string request_id,
                                          api::PageSnapshotExportFormat format,
                                          content::WebContents* selected_page) {
    base::test::TestFuture<api::PageSnapshotExportResultPtr> result;
    manager_->ExportPageSnapshot(selected_page, request_id, "selected-page",
                                 format, result.GetCallback());
    return result.Take();
  }

  api::PageSnapshotExportResultPtr Export(
      std::string request_id,
      api::PageSnapshotExportFormat format) {
    return Export(std::move(request_id), format, web_contents());
  }

  CoreServiceManager* LookupManager(content::BrowserContext* context) {
    return context == browser_context_ ? manager_.get() : nullptr;
  }

  void LoadBootstrap(uint64_t generation,
                     bool private_profile,
                     CoreEffectBroker::BootstrapCallback callback) {
    if (defer_bootstrap_) {
      deferred_bootstrap_ = std::move(callback);
      return;
    }
    std::move(callback).Run(Bootstrap(generation, private_profile));
  }

  test::FixtureOriginMap origins_;
  test::QuietManagerTail quiet_tail_;
  raw_ptr<content::BrowserContext> browser_context_ = nullptr;
  scoped_refptr<CorePageObservationBroker> observation_broker_;
  std::unique_ptr<CoreServiceManager> manager_;
  bool defer_bootstrap_ = false;
  CoreEffectBroker::BootstrapCallback deferred_bootstrap_;
};

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       NoSelectedPageRefusesBeforeColdCoreLaunch) {
  ASSERT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());

  api::PageSnapshotExportResultPtr result = Export(
      "page-export-no-selection", api::PageSnapshotExportFormat::kMarkdown,
      /*selected_page=*/nullptr);

  ASSERT_TRUE(result);
  EXPECT_EQ(api::PageSnapshotExportAvailability::kNoSelectedPage,
            result->availability);
  EXPECT_FALSE(result->snapshot_export);
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

#if BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       CancelSettlesOnceAndIgnoresLateBootstrap) {
  ASSERT_TRUE(NavigateAndAttach("static-article"));
  defer_bootstrap_ = true;
  int completions = 0;
  api::PageSnapshotExportAvailability terminal =
      api::PageSnapshotExportAvailability::kAvailable;
  manager_->ExportPageSnapshot(
      web_contents(), "page-export-cancel", "selected-page",
      api::PageSnapshotExportFormat::kMarkdown,
      base::BindLambdaForTesting([&](api::PageSnapshotExportResultPtr result) {
        ++completions;
        terminal = result->availability;
      }));
  ASSERT_TRUE(deferred_bootstrap_);
  EXPECT_TRUE(manager_->CancelPageSnapshotExport("page-export-cancel"));
  EXPECT_EQ(1, completions);
  EXPECT_EQ(api::PageSnapshotExportAvailability::kCancelled, terminal);
  std::move(deferred_bootstrap_).Run(Bootstrap(kGeneration, false));
  EXPECT_EQ(1, completions);
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       MarkdownCarriesBoundedProvenanceAndWithholdsSecrets) {
  ASSERT_TRUE(NavigateAndAttach(
      "sensitive-form", "?private-query=must-not-cross#private-fragment"));

  api::PageSnapshotExportResultPtr first = Export(
      "page-export-markdown-1", api::PageSnapshotExportFormat::kMarkdown);
  api::PageSnapshotExportResultPtr second = Export(
      "page-export-markdown-2", api::PageSnapshotExportFormat::kMarkdown);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            first->availability);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            second->availability);
  ASSERT_TRUE(first->snapshot_export);
  ASSERT_TRUE(second->snapshot_export);
  const api::PageSnapshotExportView& artifact = *first->snapshot_export;
  EXPECT_EQ("page-export-markdown-1", artifact.request_id);
  EXPECT_EQ("selected-page", artifact.document_id);
  EXPECT_EQ(api::PageSnapshotExportFormat::kMarkdown, artifact.format);
  EXPECT_EQ("text/markdown", artifact.mime_type);
  EXPECT_EQ("taffy-page-snapshot.md", artifact.suggested_file_name);
  EXPECT_EQ(
      url::Origin::Create(origins_.FixtureUrl("sensitive-form")).Serialize(),
      artifact.origin);
  EXPECT_GT(artifact.document_revision, 0u);
  EXPECT_GT(artifact.node_count, 0u);
  EXPECT_GT(artifact.redacted_field_count, 0u);
  EXPECT_GT(artifact.suppressed_secret_value_count, 0u);
  EXPECT_GT(artifact.withheld_field_count, 0u);
  EXPECT_GT(artifact.captured_at_epoch_ms, 0u);
  EXPECT_TRUE(artifact.source_query_withheld);
  EXPECT_TRUE(artifact.source_fragment_withheld);
  EXPECT_TRUE(artifact.secure_context);
  EXPECT_GE(second->snapshot_export->captured_at_epoch_ms,
            artifact.captured_at_epoch_ms);

  const std::string text = ContentText(artifact);
  const std::optional<DirectObservationContext> live =
      TaffyPageIntelligenceHost::FromWebContents(web_contents())
          ->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  EXPECT_TRUE(text.starts_with("# TaffyGo page snapshot\n\n## Provenance"));
  EXPECT_NE(std::string::npos, text.find("- Secure context: yes"));
  EXPECT_NE(std::string::npos,
            text.find("- URL disclosure: origin only"));
  EXPECT_NE(std::string::npos,
            text.find("- Source query withheld: yes"));
  EXPECT_EQ(std::string::npos, text.find("private-query"));
  EXPECT_EQ(std::string::npos, text.find("private-fragment"));
  EXPECT_NE(std::string::npos, text.find("Order summary"));
  EXPECT_NE(std::string::npos, text.find("`ACTIVATE`"));
  EXPECT_NE(std::string::npos, text.find("[withheld]"));
  EXPECT_EQ(std::string::npos, text.find(live->page_epoch));
  test::CanaryLeakScanner scanner =
      test::CanaryLeakScanner::ForFixture("sensitive-form");
  scanner.AddSink("Markdown page snapshot export", text);
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       CanonicalJsonKeepsHiddenContentEvidenceAndKeyOrder) {
  ASSERT_TRUE(NavigateAndAttach("hidden-and-offscreen"));

  api::PageSnapshotExportResultPtr first = Export(
      "page-export-json-1", api::PageSnapshotExportFormat::kCanonicalJson);
  api::PageSnapshotExportResultPtr second = Export(
      "page-export-json-2", api::PageSnapshotExportFormat::kCanonicalJson);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            first->availability);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            second->availability);
  ASSERT_TRUE(first->snapshot_export);
  ASSERT_TRUE(second->snapshot_export);
  const api::PageSnapshotExportView& artifact = *first->snapshot_export;
  EXPECT_EQ("page-export-json-1", artifact.request_id);
  EXPECT_EQ("selected-page", artifact.document_id);
  EXPECT_EQ(api::PageSnapshotExportFormat::kCanonicalJson, artifact.format);
  EXPECT_EQ("application/json", artifact.mime_type);
  EXPECT_EQ("taffy-page-snapshot.json", artifact.suggested_file_name);
  EXPECT_EQ(url::Origin::Create(origins_.FixtureUrl("hidden-and-offscreen"))
                .Serialize(),
            artifact.origin);
  EXPECT_GT(artifact.document_revision, 0u);
  EXPECT_GT(artifact.node_count, 0u);
  EXPECT_GT(artifact.captured_at_epoch_ms, 0u);
  EXPECT_FALSE(artifact.source_query_withheld);
  EXPECT_FALSE(artifact.source_fragment_withheld);
  EXPECT_TRUE(artifact.secure_context);
  EXPECT_GE(second->snapshot_export->captured_at_epoch_ms,
            artifact.captured_at_epoch_ms);

  const std::string text = ContentText(artifact);
  const std::optional<DirectObservationContext> live =
      TaffyPageIntelligenceHost::FromWebContents(web_contents())
          ->BuildDirectObservationContext();
  ASSERT_TRUE(live);
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  EXPECT_TRUE(parsed->is_dict());
  EXPECT_TRUE(
      text.starts_with("{\"format\":\"taffy-page-snapshot-v1\",\"nodes\":["));
  const size_t actions = text.find("\"actions\"");
  const size_t signals = text.find("\"content_signals\"");
  const size_t trust = text.find("\"content_trust\"");
  const size_t display_id = text.find("\"display_id\"");
  ASSERT_NE(std::string::npos, actions);
  ASSERT_NE(std::string::npos, signals);
  ASSERT_NE(std::string::npos, trust);
  ASSERT_NE(std::string::npos, display_id);
  EXPECT_LT(actions, signals);
  EXPECT_LT(signals, trust);
  EXPECT_LT(trust, display_id);
  EXPECT_NE(std::string::npos, text.find("Visible price"));
  EXPECT_NE(std::string::npos, text.find("HIDDEN_BY_STYLE"));
  EXPECT_NE(std::string::npos, text.find("\"secure_context\":true"));
  EXPECT_NE(std::string::npos,
            text.find("\"source_url_disclosure\":\"origin_only\""));
  EXPECT_EQ(std::string::npos, text.find(live->page_epoch));
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       ReusedRequestReturnsTheFirstExactArtifact) {
  ASSERT_TRUE(NavigateAndAttach("static-article"));

  api::PageSnapshotExportResultPtr first = Export(
      "page-export-replay", api::PageSnapshotExportFormat::kMarkdown);
  api::PageSnapshotExportResultPtr replay = Export(
      "page-export-replay", api::PageSnapshotExportFormat::kMarkdown);

  ASSERT_TRUE(first);
  ASSERT_TRUE(replay);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            first->availability);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            replay->availability);
  ASSERT_TRUE(first->snapshot_export);
  ASSERT_TRUE(replay->snapshot_export);
  EXPECT_EQ(first->snapshot_export->content, replay->snapshot_export->content);
  EXPECT_EQ(first->snapshot_export->captured_at_epoch_ms,
            replay->snapshot_export->captured_at_epoch_ms);
  EXPECT_EQ("page-export-replay", replay->snapshot_export->request_id);
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       ReusedRequestWithChangedFormatIsAContentFreeConflict) {
  ASSERT_TRUE(NavigateAndAttach("static-article"));

  api::PageSnapshotExportResultPtr first = Export(
      "page-export-conflict", api::PageSnapshotExportFormat::kMarkdown);
  api::PageSnapshotExportResultPtr conflict = Export(
      "page-export-conflict", api::PageSnapshotExportFormat::kCanonicalJson);

  ASSERT_TRUE(first);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            first->availability);
  ASSERT_TRUE(first->snapshot_export);
  ASSERT_TRUE(conflict);
  EXPECT_EQ(api::PageSnapshotExportAvailability::kReplayConflict,
            conflict->availability);
  EXPECT_FALSE(conflict->snapshot_export);
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       CurrentDocumentExcludesCrossOriginFrameContent) {
  ASSERT_TRUE(NavigateAndAttach("iframe-cross-origin"));

  api::PageSnapshotExportResultPtr result =
      Export("page-export-cross-origin",
             api::PageSnapshotExportFormat::kCanonicalJson);

  ASSERT_TRUE(result);
  ASSERT_EQ(api::PageSnapshotExportAvailability::kAvailable,
            result->availability);
  ASSERT_TRUE(result->snapshot_export);
  const std::string text = ContentText(*result->snapshot_export);
  EXPECT_NE(std::string::npos, text.find("listing is on the primary origin"));
  EXPECT_EQ(std::string::npos, text.find("Partner listing price"));
  EXPECT_EQ(std::string::npos, text.find("Embed-origin fact"));
  test::CanaryLeakScanner scanner =
      test::CanaryLeakScanner::ForFixture("iframe-cross-origin");
  scanner.AddSink("cross-origin page snapshot export", text);
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

IN_PROC_BROWSER_TEST_F(PageSnapshotExportBrowserTest,
                       BudgetTruncationIsAnExactContentFreeTerminal) {
  ASSERT_TRUE(NavigateAndAttach("oversized-document"));

  api::PageSnapshotExportResultPtr result =
      Export("page-export-oversized", api::PageSnapshotExportFormat::kMarkdown);

  ASSERT_TRUE(result);
  EXPECT_EQ(api::PageSnapshotExportAvailability::kIncomplete,
            result->availability);
  EXPECT_FALSE(result->snapshot_export);
}

#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace
}  // namespace taffy
