// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_PLATFORM_H_
#define TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_PLATFORM_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"

namespace content {
class NavigationUIData;
}  // namespace content

namespace taffy {

// Embedder carrier for an exact task-navigation destination. The portable
// authority owns normalization and admission; this interface owns only the
// product-specific NavigationUIData representation used before request start.
class TaskNavigationAuthorityPlatform {
 public:
  virtual ~TaskNavigationAuthorityPlatform() = default;

  virtual std::unique_ptr<content::NavigationUIData> CreateNavigationData(
      std::string exact_destination) const = 0;
  virtual std::optional<std::string> ReadExactDestination(
      const content::NavigationUIData& navigation_data) const = 0;
};

// Installs one process-lifetime product implementation. Android installs its
// Chrome-typed carrier through the product-only JNI composition edge before a
// task source window can exist. With no implementation, navigation fails
// closed and browser_shared remains linkable without Chrome implementation
// targets.
void InstallTaskNavigationAuthorityPlatform(
    const TaskNavigationAuthorityPlatform* platform);
const TaskNavigationAuthorityPlatform* GetTaskNavigationAuthorityPlatform();

// Unit tests exercise the portable authority with an opaque fake carrier.
// Scoped replacement keeps independent suites from sharing process state.
class ScopedTaskNavigationAuthorityPlatformForTesting final {
 public:
  explicit ScopedTaskNavigationAuthorityPlatformForTesting(
      const TaskNavigationAuthorityPlatform* platform);
  ScopedTaskNavigationAuthorityPlatformForTesting(
      const ScopedTaskNavigationAuthorityPlatformForTesting&) = delete;
  ScopedTaskNavigationAuthorityPlatformForTesting& operator=(
      const ScopedTaskNavigationAuthorityPlatformForTesting&) = delete;
  ~ScopedTaskNavigationAuthorityPlatformForTesting();

 private:
  const raw_ptr<const TaskNavigationAuthorityPlatform> platform_;
  const raw_ptr<const TaskNavigationAuthorityPlatform> previous_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_NAVIGATION_AUTHORITY_PLATFORM_H_
