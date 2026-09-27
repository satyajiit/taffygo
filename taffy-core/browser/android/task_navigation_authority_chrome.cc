// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/task_navigation_authority_chrome.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/no_destructor.h"
#include "chrome/browser/renderer_host/chrome_navigation_ui_data.h"
#include "content/public/browser/navigation_ui_data.h"
#include "taffy/browser/task_navigation_authority_platform.h"

namespace taffy {
namespace {

class ChromeTaskNavigationAuthorityPlatform final
    : public TaskNavigationAuthorityPlatform {
 public:
  std::unique_ptr<content::NavigationUIData> CreateNavigationData(
      std::string exact_destination) const override {
    auto data = std::make_unique<ChromeNavigationUIData>();
    data->set_taffy_task_navigation_target(std::move(exact_destination));
    return data;
  }

  std::optional<std::string> ReadExactDestination(
      const content::NavigationUIData& navigation_data) const override {
    // ChromeContentBrowserClient is the only production reader, and every
    // NavigationUIData in that funnel is ChromeNavigationUIData. The cast is
    // confined to this Chrome-linked product adapter.
    const auto& chrome_data =
        static_cast<const ChromeNavigationUIData&>(navigation_data);
    return chrome_data.taffy_task_navigation_target();
  }
};

ChromeTaskNavigationAuthorityPlatform& ChromePlatform() {
  static base::NoDestructor<ChromeTaskNavigationAuthorityPlatform> platform;
  return *platform;
}

}  // namespace

void InstallChromeTaskNavigationAuthorityPlatform() {
  InstallTaskNavigationAuthorityPlatform(&ChromePlatform());
}

}  // namespace taffy
