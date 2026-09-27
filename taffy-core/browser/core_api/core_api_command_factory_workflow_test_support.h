// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_WORKFLOW_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_WORKFLOW_TEST_SUPPORT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

// The fixed entropy and the consent shapes the workflow factory suites drive:
// one selection, one comparison and the two errand shapes, each with the
// resolved consent the browser would answer for it. Shared so the two suites
// build the same commands and neither redefines the other's fixture.

namespace taffy {

class FixedWorkflowEntropy final : public CoreApiEntropySource {
 public:
  FixedWorkflowEntropy();
  ~FixedWorkflowEntropy() override;

  std::string NewOpaqueId(std::string_view domain) override;
  std::array<uint8_t, 32> NewTaskSeed() override;
};

inline std::vector<std::string> ReviewedTools() {
  return {"browser.dom.read"};
}

inline std::vector<std::string> ErrandTools() {
  return {"browser.tabs", "browser.search", "browser.dom.read",
          "browser.link.open", "browser.navigate", "browser.form.inspect",
          "browser.selection.read", "user.ask", "user.request_values",
          "user.handover"};
}

inline core_api::mojom::TaskConsentPreviewPtr SelectionIntent() {
  return core_api::mojom::TaskConsentPreview::New(
      std::vector<std::string>{"example.test"}, false, 0u,
      core_api::mojom::TaskProviderRoute::kNoModelRequired, std::vector<core_api::mojom::TaskAttachedStore>{});
}

inline core_service::mojom::TaskConsentPreviewPtr ResolvedConsent() {
  std::vector<core_service::mojom::TaskConsentSourcePtr> sources;
  sources.push_back(core_service::mojom::TaskConsentSource::New(
      "source-fixed", "tab-fixed", "https://example.test", std::nullopt));
  return core_service::mojom::TaskConsentPreview::New(
      std::move(sources), false, 0u,
      core_service::mojom::TaskProviderRoute::kNoModelRequired);
}

inline core_api::mojom::TaskConsentPreviewPtr ComparisonIntent() {
  return core_api::mojom::TaskConsentPreview::New(
      std::vector<std::string>{"example.test", "second.example"}, false, 0u,
      core_api::mojom::TaskProviderRoute::kDirectUserKey, std::vector<core_api::mojom::TaskAttachedStore>{});
}

inline core_service::mojom::TaskConsentPreviewPtr ResolvedComparison() {
  std::vector<core_service::mojom::TaskConsentSourcePtr> sources;
  sources.push_back(core_service::mojom::TaskConsentSource::New(
      "source-a", "tab-a", "https://example.test", std::nullopt));
  sources.push_back(core_service::mojom::TaskConsentSource::New(
      "source-b", "tab-b", "https://second.example", std::nullopt));
  return core_service::mojom::TaskConsentPreview::New(
      std::move(sources), false, 0u,
      core_service::mojom::TaskProviderRoute::kDirectUserKey);
}

inline core_api::mojom::TaskConsentPreviewPtr ErrandIntent(
    core_api::mojom::TaskProviderRoute route,
    std::vector<std::string> source_hosts = {}) {
  return core_api::mojom::TaskConsentPreview::New(
      std::move(source_hosts), true, 4u, route, std::vector<core_api::mojom::TaskAttachedStore>{});
}

inline core_service::mojom::TaskConsentPreviewPtr ResolvedErrand(
    core_service::mojom::TaskProviderRoute route,
    bool with_source = false) {
  std::vector<core_service::mojom::TaskConsentSourcePtr> sources;
  if (with_source) {
    sources.push_back(core_service::mojom::TaskConsentSource::New(
        "source-fixed", "tab-fixed", "https://example.test", std::nullopt));
  }
  return core_service::mojom::TaskConsentPreview::New(std::move(sources), true,
                                                       4u, route);
}

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_CORE_API_COMMAND_FACTORY_WORKFLOW_TEST_SUPPORT_H_
