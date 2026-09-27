// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <memory>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "taffy/browser/android/page_inspector_jni_headers/TaffyPageInspectorBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy {
namespace {

namespace api_mojom = core_api::mojom;

class PageInspectorBridge final : public api_mojom::TaffyPageInspector,
                                  public content::WebContentsObserver {
 public:
  PageInspectorBridge(Profile* profile, content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        profile_(profile),
        web_contents_(web_contents) {}

  PageInspectorBridge(const PageInspectorBridge&) = delete;
  PageInspectorBridge& operator=(const PageInspectorBridge&) = delete;
  ~PageInspectorBridge() override = default;

  void GetDocuments(GetDocumentsCallback callback) override {
    CoreServiceManager* manager = Manager();
    if (!manager) {
      auto result = api_mojom::PageInspectorDocumentsView::New();
      result->availability =
          api_mojom::PageInspectorAvailability::kNoSelectedPage;
      std::move(callback).Run(std::move(result));
      return;
    }
    std::move(callback).Run(manager->GetPageInspectorDocuments(web_contents_));
  }

  void GetSnapshot(const std::string& document_id,
                   GetSnapshotCallback callback) override {
    CoreServiceManager* manager = Manager();
    if (!manager || document_id != "selected-page") {
      auto result = api_mojom::PageInspectorSnapshotResult::New();
      result->availability =
          manager ? api_mojom::PageInspectorAvailability::kInvalidResponse
                  : api_mojom::PageInspectorAvailability::kNoSelectedPage;
      std::move(callback).Run(std::move(result));
      return;
    }
    manager->ObservePageForInspector(web_contents_, std::move(callback));
  }

  void ExportSnapshot(const std::string& request_id,
                      const std::string& document_id,
                      api_mojom::PageSnapshotExportFormat format,
                      ExportSnapshotCallback callback) override {
    CoreServiceManager* manager = Manager();
    if (!manager) {
      auto result = api_mojom::PageSnapshotExportResult::New();
      result->availability =
          api_mojom::PageSnapshotExportAvailability::kNoSelectedPage;
      std::move(callback).Run(std::move(result));
      return;
    }
    manager->ExportPageSnapshot(web_contents_, request_id, document_id, format,
                                std::move(callback));
  }

  void CancelExport(const std::string& request_id,
                    CancelExportCallback callback) override {
    CoreServiceManager* manager = Manager();
    const bool accepted =
        manager && manager->CancelPageSnapshotExport(request_id);
    std::move(callback).Run(
        accepted ? api_mojom::CoreApiSubmissionStatus::kAccepted
                 : api_mojom::CoreApiSubmissionStatus::kInvalidRequest);
  }

  void WebContentsDestroyed() override { web_contents_ = nullptr; }

 private:
  CoreServiceManager* Manager() const {
    if (!profile_ || !web_contents_ ||
        web_contents_->GetBrowserContext() !=
            static_cast<content::BrowserContext*>(profile_.get())) {
      return nullptr;
    }
    return CoreServiceManagerFactory::GetForProfile(profile_);
  }

  const raw_ptr<Profile> profile_;
  raw_ptr<content::WebContents> web_contents_;
};

}  // namespace

static jlong JNI_TaffyPageInspectorBridge_Connect(
    JNIEnv* env,
    Profile* profile,
    content::WebContents* web_contents) {
  if (!profile || !web_contents ||
      web_contents->GetBrowserContext() !=
          static_cast<content::BrowserContext*>(profile) ||
      !TaffyPageIntelligenceHost::IsEligible(web_contents)) {
    return 0;
  }
  TaffyPageIntelligenceHost::AttachIfEligible(web_contents);
  mojo::PendingRemote<api_mojom::TaffyPageInspector> remote;
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<PageInspectorBridge>(profile, web_contents),
      remote.InitWithNewPipeAndPassReceiver());
  return remote.PassPipe().release().value();
}

DEFINE_JNI(TaffyPageInspectorBridge)

}  // namespace taffy
