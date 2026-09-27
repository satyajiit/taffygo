// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_ERRAND_DOWNLOAD_TEST_PAGE_H_
#define TAFFY_TEST_RECOVERY_ERRAND_DOWNLOAD_TEST_PAGE_H_

#include <stdint.h>

#include <memory>
#include <string>

class Profile;

namespace base {
class ScopedTempDir;
}  // namespace base

namespace content {
class DownloadManager;
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace net::test_server {
struct HttpRequest;
class HttpResponse;
}  // namespace net::test_server

namespace taffy {
class CoreServiceManager;
}  // namespace taffy

namespace taffy::core_api::mojom {
class TaffyProfileCoreApi;
}  // namespace taffy::core_api::mojom

namespace taffy::test {

class CoreApiStatusObserver;
class ErrandTaskModelEndpoint;

std::string ErrandFixturePdf();
void ConfigureErrandDownloadDirectory(Profile& profile,
                                      base::ScopedTempDir& directory);
void ExpectErrandFixturePdf(const download::DownloadItem& file);
void ExpectErrandTaskDownloads(CoreServiceManager& core,
                               content::DownloadManager& downloads,
                               const std::string& first_task_id,
                               const std::string& replay_task_id);
void MaybeOpenErrandTaskPdf(content::WebContents& contents,
                            CoreServiceManager& core,
                            const std::string& replay_task_id);
// Closed state only: no address, path, filename, or download identity.
std::string ErrandDownloadDiagnostic(const download::DownloadItem* file);
void RestartErrandCore(CoreServiceManager& core,
                       const CoreApiStatusObserver& observer);
void DisableErrandFlowAndRestart(CoreServiceManager& core,
                                 core_api::mojom::TaffyProfileCoreApi& remote,
                                 const CoreApiStatusObserver& observer,
                                 const std::string& first_task_id);
// Fails immediately on terminal refusal or withdrawn readiness, with only
// closed task/download state. Completion still requires the exact replay.
void WaitForErrandReplayCompletion(const CoreApiStatusObserver& observer,
                                   const std::string& first_task_id,
                                   content::DownloadManager* downloads,
                                   const ErrandTaskModelEndpoint& endpoint,
                                   uint32_t first_task_model_count);
std::unique_ptr<net::test_server::HttpResponse> ServeErrandDownloadFixture(
    const net::test_server::HttpRequest& request);

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_ERRAND_DOWNLOAD_TEST_PAGE_H_
