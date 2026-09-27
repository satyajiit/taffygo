// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_api/saved_flow_query_projection.h"
#include "taffy/browser/core_api/saved_flow_start_navigation.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {
namespace {
namespace api = core_api::mojom;
namespace service = core_service::mojom;
bool IsSavedFlowRequestId(const std::string& id) {
  return !id.empty() && id.size() <= api::kMaxIdentifierBytes &&
         id.find_first_of("\r\n\t") == std::string::npos;
}

}  // namespace

void ProfileCoreApiFacade::FindSavedFlows(const std::string& request_id,
                                          const std::string& goal,
                                          FindSavedFlowsCallback callback) {
  QuerySavedFlowReviews(request_id, service::SavedFlowQueryKind::kExactGoal,
                        goal, std::string(), 0u, std::move(callback));
}
void ProfileCoreApiFacade::GetSavedFlowReview(
    const std::string& request_id,
    const std::string& skill_id,
    uint32_t expected_version,
    GetSavedFlowReviewCallback callback) {
  QuerySavedFlowReviews(request_id, service::SavedFlowQueryKind::kReview,
                        std::string(), skill_id, expected_version,
                        std::move(callback));
}
void ProfileCoreApiFacade::QuerySavedFlowReviews(
    const std::string& request_id,
    service::SavedFlowQueryKind kind,
    const std::string& goal,
    const std::string& skill_id,
    uint32_t expected_version,
    base::OnceCallback<void(api::SavedFlowQueryResultPtr)> callback) {
  const uint64_t ticket = ++saved_flow_query_ticket_;
  const uint64_t generation = manager_ ? manager_->service_generation() : 0u;
  if (!manager_ || !IsSavedFlowRequestId(request_id)) {
    auto result = ProjectSavedFlowQuery(request_id, generation, nullptr);
    result->availability =
        IsSavedFlowRequestId(request_id)
            ? api::SavedFlowQueryAvailability::kUnavailable
            : api::SavedFlowQueryAvailability::kInvalidRequest;
    std::move(callback).Run(std::move(result));
    return;
  }
  manager_->QuerySavedFlows(
      kind, goal, skill_id, expected_version,
      base::BindOnce(
          [](base::WeakPtr<ProfileCoreApiFacade> self, uint64_t ticket,
             const std::string& request_id, uint64_t generation,
             base::OnceCallback<void(api::SavedFlowQueryResultPtr)> callback,
             service::SavedFlowQueryResultPtr reply) {
            const bool stale =
                !self || !self->manager_ ||
                self->saved_flow_query_ticket_ != ticket ||
                self->manager_->service_generation() != generation;
            auto result = ProjectSavedFlowQuery(
                request_id, generation, stale ? nullptr : std::move(reply));
            if (stale) {
              result->availability =
                  api::SavedFlowQueryAvailability::kStaleRequest;
            }
            std::move(callback).Run(std::move(result));
          },
          weak_factory_.GetWeakPtr(), ticket, request_id, generation,
          std::move(callback)));
}

void ProfileCoreApiFacade::OpenSavedFlowStart(
    const std::string& request_id,
    const std::string& skill_id,
    uint32_t expected_version,
    OpenSavedFlowStartCallback callback) {
  const uint64_t ticket = ++saved_flow_query_ticket_;
  content::WebContents* tab =
      manager_ ? manager_->SelectedSavedFlowStartTab() : nullptr;
  if (!IsSavedFlowRequestId(request_id) || !tab) {
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  const uint64_t generation = manager_->service_generation();
  const uint64_t selection_revision =
      manager_->SavedFlowStartSelectionRevision();
  manager_->QuerySavedFlows(
      service::SavedFlowQueryKind::kPublicStart, std::string(), skill_id,
      expected_version,
      base::BindOnce(
          [](base::WeakPtr<ProfileCoreApiFacade> self, uint64_t ticket,
             uint64_t generation, uint64_t selection_revision,
             base::WeakPtr<content::WebContents> tab, std::string skill_id,
             uint32_t expected_version, OpenSavedFlowStartCallback callback,
             service::SavedFlowQueryResultPtr result) {
            if (!self || !self->manager_ ||
                self->saved_flow_query_ticket_ != ticket ||
                self->manager_->service_generation() != generation || !tab ||
                self->manager_->SelectedSavedFlowStartTab() != tab.get() ||
                self->manager_->SavedFlowStartSelectionRevision() !=
                    selection_revision ||
                !result ||
                result->status != service::SavedFlowQueryStatus::kAvailable ||
                result->flows.size() != 1u || !result->flows.front() ||
                result->flows.front()->skill_id != skill_id ||
                result->flows.front()->active_version != expected_version) {
              std::move(callback).Run(SubmissionStatus::kInvalidRequest);
              return;
            }
            auto address =
                ReviewedSavedFlowStartAddress(*result->flows.front());
            if (!address) {
              std::move(callback).Run(SubmissionStatus::kInvalidRequest);
              return;
            }
            NavigateSavedFlowStart(
                tab.get(), *address,
                base::BindOnce(
                    [](base::WeakPtr<ProfileCoreApiFacade> self,
                       uint64_t ticket, uint64_t generation,
                       uint64_t selection_revision,
                       base::WeakPtr<content::WebContents> tab,
                       OpenSavedFlowStartCallback callback, bool committed) {
                      const bool current =
                          self && self->manager_ && tab &&
                          self->saved_flow_query_ticket_ == ticket &&
                          self->manager_->availability() ==
                              CoreServiceManager::Availability::kReady &&
                          self->manager_->service_generation() == generation &&
                          self->manager_->SelectedSavedFlowStartTab() ==
                              tab.get() &&
                          self->manager_->SavedFlowStartSelectionRevision() ==
                              selection_revision;
                      std::move(callback).Run(
                          committed && current
                              ? SubmissionStatus::kAccepted
                              : SubmissionStatus::kInvalidRequest);
                    },
                    self, ticket, generation, selection_revision, tab,
                    std::move(callback)));
          },
          weak_factory_.GetWeakPtr(), ticket, generation, selection_revision,
          tab->GetWeakPtr(), skill_id, expected_version, std::move(callback)));
}
}  // namespace taffy
