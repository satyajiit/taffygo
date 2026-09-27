// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "taffy/browser/core_api/saved_flow_start_navigation.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/referrer.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace service = core_service::mojom;
std::optional<GURL> ReviewedSavedFlowStartAddress(
    const service::SavedFlowReview& flow) {
  if (flow.provenance != service::SkillProvenance::kRecordedFromTask ||
      flow.status != service::SkillStatus::kActive ||
      !flow.recorded_from_task_id || flow.reviewed_steps.empty() ||
      flow.reviewed_steps.size() != flow.step_count ||
      !flow.reviewed_steps.front() ||
      flow.reviewed_steps.front()->verb != "browser.navigate") {
    return std::nullopt;
  }
  for (const auto& arg : flow.reviewed_steps.front()->arguments) {
    if (!arg || arg->kind != service::SkillArgumentKind::kPublicAddress ||
        !arg->public_address) {
      continue;
    }
    const GURL address(*arg->public_address);
    if (address.is_valid() && address.SchemeIs("https") &&
        !address.has_username() && !address.has_password() &&
        !address.has_query() && !address.has_ref() &&
        address.spec() == *arg->public_address &&
        url::Origin::Create(address).Serialize() == flow.origin) {
      return address;
    }
  }
  return std::nullopt;
}

namespace {
class SavedFlowStartNavigation final : public content::WebContentsObserver {
 public:
  SavedFlowStartNavigation(content::WebContents* contents,
                           GURL address,
                           int64_t navigation_id,
                           base::OnceCallback<void(bool)> callback)
      : content::WebContentsObserver(contents),
        address_(std::move(address)),
        navigation_id_(navigation_id),
        callback_(std::move(callback)) {
    timer_.Start(FROM_HERE, base::Seconds(30),
                 base::BindOnce(&SavedFlowStartNavigation::Finish,
                                base::Unretained(this), false));
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() &&
        handle->GetNavigationId() == navigation_id_) {
      Finish(handle->HasCommitted() && !handle->IsErrorPage() &&
             !handle->WasServerRedirect() && handle->GetURL() == address_);
    }
  }
  void WebContentsDestroyed() override { Finish(false); }

 private:
  void Finish(bool committed) {
    auto callback = std::move(callback_);
    Observe(nullptr);
    delete this;
    std::move(callback).Run(committed);
  }
  const GURL address_;
  const int64_t navigation_id_;
  base::OnceCallback<void(bool)> callback_;
  base::OneShotTimer timer_;
};
}  // namespace
void NavigateSavedFlowStart(content::WebContents* contents,
                            const GURL& address,
                            base::OnceCallback<void(bool)> callback) {
  if (!contents) {
    std::move(callback).Run(false);
    return;
  }
  content::NavigationController::LoadURLParams params(address);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  params.has_user_gesture = true;
  auto navigation = contents->GetController().LoadURLWithParams(params);
  if (!navigation) {
    std::move(callback).Run(false);
    return;
  }
  new SavedFlowStartNavigation(contents, address, navigation->GetNavigationId(),
                               std::move(callback));
}
}  // namespace taffy
