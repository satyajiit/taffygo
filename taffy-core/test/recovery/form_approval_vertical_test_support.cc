// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/form_approval_vertical_test_support.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "content/public/browser/web_contents.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_api/task_workflow_tools.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/recovery/form_approval_vertical_test_internal.h"

namespace taffy::test {

namespace api = core_api::mojom;

FormApprovalVerticalHarness::Impl::Impl(CoreServiceManager* manager,
                                        content::WebContents* web_contents)
    : manager_(manager), web_contents_(web_contents), session_(manager) {}

FormApprovalVerticalHarness::Impl::~Impl() {
  surface_.reset();
  if (manager_ && window_token_ != 0u) {
    manager_->DeactivateTaskSourceWindow(window_token_);
    manager_->UnregisterTaskSourceWindow(window_token_);
  }
  if (manager_ && observing_) {
    manager_->RemoveObserver(&observer_);
  }
}

bool FormApprovalVerticalHarness::Impl::Initialize() {
  if (!manager_ || !web_contents_ ||
      manager_->availability() != CoreServiceManager::Availability::kStopped) {
    return false;
  }
  session_.Bind(CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_));
  CoreServiceManagerTaskEffectTestPeer::BeginBrowserAuthorityGeneration(
      *manager_, "form-vertical-profile");
  CoreServiceManagerTaskEffectTestPeer::ActivateEffectBroker(*manager_);
  manager_->AddObserver(&observer_);
  observing_ = true;

  manager_->BindFieldValueSurface(surface_.BindNewPipeAndPassReceiver());
  surface_->Connect(field_client_.Bind());
  surface_.FlushForTesting();

  return RegisterAndPublish(
      MakeEmptyFormBindings(manager_->service_generation(), 1u), 1u);
}

bool FormApprovalVerticalHarness::Impl::DiscoverForm() {
  base::test::TestFuture<api::PageInspectorSnapshotResultPtr> future;
  manager_->ObservePageForInspector(web_contents_, future.GetCallback());
  api::PageInspectorSnapshotResultPtr result = future.Take();
  if (!result ||
      result->availability != api::PageInspectorAvailability::kAvailable ||
      !base::test::RunUntil(
          [this]() { return session_.target().has_value(); })) {
    return false;
  }
  target_ = *session_.target();
  return !target_.tab_id.empty() && !target_.form_node_id.empty() &&
         !target_.field_node_id.empty() && target_.graph_revision != 0u;
}

bool FormApprovalVerticalHarness::Impl::StartWebErrand(
    std::string source_host) {
  if (target_.tab_id.empty() || source_host.empty() || window_token_ != 0u) {
    return false;
  }
  window_token_ = manager_->RegisterTaskSourceWindow();
  if (window_token_ == 0u ||
      !manager_->RegisterTaskSourceTab(window_token_, 71, web_contents_) ||
      !manager_->SelectTaskSourceTab(window_token_, 71, web_contents_) ||
      !manager_->ActivateTaskSourceWindow(window_token_)) {
    return false;
  }

  auto intent = api::TaskConsentPreview::New();
  intent->source_hosts = {std::move(source_host)};
  intent->source_discovery_enabled = true;
  intent->new_source_cap = 1u;
  intent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  std::optional<service::TaskConsentPreviewPtr> resolved =
      manager_->ResolveStartTaskConsent(api::TaskTemplateId::kWebErrand,
                                        *intent);
  if (!resolved || !*resolved || (*resolved)->sources.size() != 1u ||
      !(*resolved)->sources.front() ||
      (*resolved)->sources.front()->tab_id != target_.tab_id) {
    return false;
  }

  CoreApiCommandFactory factory(manager_->browser_profile_id(),
                                CreateCoreApiEntropySource());
  std::optional<ProjectedCoreCommand> projected = factory.BuildStartTask(
      "Fill the selected fixture form", api::TaskTemplateId::kWebErrand,
      std::nullopt, std::move(intent), std::move(*resolved),
      WorkflowToolsForStart(api::TaskTemplateId::kWebErrand,
                            api::TaskProviderRoute::kDirectUserKey, {}),
      manager_->browser_session_id(), manager_->service_generation(),
      FormTestMonotonicMillis());
  if (!projected || !projected->core_service_command ||
      !projected->core_service_command->start_task) {
    return false;
  }
  start_command_ = projected->core_service_command.Clone();
  task_id_ = start_command_->start_task->task_id;

  base::test::TestFuture<service::AdmissionPtr> admission;
  manager_->Submit(std::move(projected->core_service_command),
                   admission.GetCallback());
  service::AdmissionPtr result = admission.Take();
  if (!result || result->status != service::AdmissionStatus::kAccepted ||
      !CoreServiceManagerTaskEffectTestPeer::CompleteStagedStorageCommit(
          *manager_, *start_command_, task_id_, 1u,
          FormTestMonotonicMillis())) {
    return false;
  }
  return RegisterAndPublish(
      MakeTaskFormBindings(manager_->service_generation(), 2u, task_id_, 1u,
                           *start_command_->start_task, std::nullopt,
                           std::nullopt, nullptr),
      2u);
}

bool FormApprovalVerticalHarness::Impl::OpenFieldValueRequest(
    std::string request_id) {
  if (!start_command_ || request_id.empty()) {
    return false;
  }
  request_id_ = std::move(request_id);
  base::test::TestFuture<service::TaskEffectCompletionPtr> future;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_,
      MakeFieldValueRequestEffect(manager_->service_generation(), 1u, task_id_,
                                  request_id_, target_,
                                  FormTestMonotonicMillis()),
      future.GetCallback());
  service::TaskEffectCompletionPtr completion = future.Take();
  if (!completion ||
      completion->status != service::TaskEffectCompletionStatus::kSucceeded ||
      !base::test::RunUntil(
          [this]() { return field_client_.request() != nullptr; })) {
    return false;
  }
  const surface::FieldValueRequest& request = *field_client_.request();
  if (request.request_id != request_id_ || request.task_id != task_id_ ||
      request.fields.size() != 1u || !request.fields.front() ||
      request.fields.front()->field_id != target_.field_node_id ||
      request.approval_lifetime_seconds == 0u) {
    return false;
  }
  field_ids_.clear();
  field_labels_.clear();
  for (const auto& field : request.fields) {
    field_ids_.push_back(field->field_id);
    field_labels_.push_back(field->label);
  }
  approval_lifetime_seconds_ = request.approval_lifetime_seconds;
  // A strictly newer publication with the same task revision is the Core
  // Service's acknowledgement that RequestFieldValues completed. Only that
  // witness can release a response that raced the acknowledgement; this test
  // publishes it before Supply, exercising the ordinary already-acknowledged
  // path while the deferred-owner unit suite covers the opposite ordering.
  return RegisterAndPublish(
      MakeTaskFormBindings(manager_->service_generation(), 3u, task_id_, 1u,
                           *start_command_->start_task, std::nullopt,
                           std::nullopt, nullptr),
      3u);
}

bool FormApprovalVerticalHarness::Impl::RegisterAndPublish(
    service::CoreStateBrowserBindingsPtr bindings,
    uint64_t sequence) {
  if (CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
          *manager_, std::move(bindings)) !=
      service::PendingApprovalRegistrationStatus::kRegistered) {
    return false;
  }
  CoreServiceManagerTaskEffectTestPeer::Publish(
      *manager_, MakeFormState(manager_->service_generation(), sequence));
  return true;
}

const LiveFormApprovalTarget& FormApprovalVerticalHarness::Impl::target()
    const {
  return target_;
}
const std::string& FormApprovalVerticalHarness::Impl::task_id() const {
  return task_id_;
}
const std::string& FormApprovalVerticalHarness::Impl::request_id() const {
  return request_id_;
}
const std::vector<std::string>& FormApprovalVerticalHarness::Impl::field_ids()
    const {
  return field_ids_;
}
const std::vector<std::string>&
FormApprovalVerticalHarness::Impl::field_labels() const {
  return field_labels_;
}
uint32_t FormApprovalVerticalHarness::Impl::approval_lifetime_seconds() const {
  return approval_lifetime_seconds_;
}

FormApprovalVerticalHarness::FormApprovalVerticalHarness(
    CoreServiceManager* manager,
    content::WebContents* web_contents)
    : impl_(std::make_unique<Impl>(manager, web_contents)) {}

FormApprovalVerticalHarness::~FormApprovalVerticalHarness() = default;

bool FormApprovalVerticalHarness::Initialize() {
  return impl_->Initialize();
}
bool FormApprovalVerticalHarness::DiscoverForm() {
  return impl_->DiscoverForm();
}
bool FormApprovalVerticalHarness::StartWebErrand(std::string source_host) {
  return impl_->StartWebErrand(std::move(source_host));
}
bool FormApprovalVerticalHarness::OpenFieldValueRequest(
    std::string request_id) {
  return impl_->OpenFieldValueRequest(std::move(request_id));
}
const LiveFormApprovalTarget& FormApprovalVerticalHarness::target() const {
  return impl_->target();
}
const std::string& FormApprovalVerticalHarness::task_id() const {
  return impl_->task_id();
}
const std::string& FormApprovalVerticalHarness::request_id() const {
  return impl_->request_id();
}
const std::vector<std::string>& FormApprovalVerticalHarness::field_ids() const {
  return impl_->field_ids();
}
const std::vector<std::string>& FormApprovalVerticalHarness::field_labels()
    const {
  return impl_->field_labels();
}
uint32_t FormApprovalVerticalHarness::approval_lifetime_seconds() const {
  return impl_->approval_lifetime_seconds();
}
surface::FieldValueSupplyVerdict FormApprovalVerticalHarness::Supply(
    std::vector<std::string> values) {
  return impl_->Supply(std::move(values));
}
bool FormApprovalVerticalHarness::WaitForSuppliedCount(
    uint32_t expected_count) {
  return impl_->WaitForSuppliedCount(expected_count);
}
bool FormApprovalVerticalHarness::ConfirmFillProposal(
    std::string action_id,
    std::string proposal_digest,
    uint32_t supplied_value_index) {
  return impl_->ConfirmFillProposal(
      std::move(action_id), std::move(proposal_digest), supplied_value_index);
}
service::PolicyEvaluationStatus FormApprovalVerticalHarness::AuthorizeFill() {
  return impl_->AuthorizeFill();
}
service::TaskEffectCompletionStatus
FormApprovalVerticalHarness::DispatchFill() {
  return impl_->DispatchFill();
}
service::TaskEffectCompletionStatus
FormApprovalVerticalHarness::ReplayDispatch() {
  return impl_->ReplayDispatch();
}
size_t FormApprovalVerticalHarness::submitted_command_count() const {
  return impl_->submitted_command_count();
}
uint32_t FormApprovalVerticalHarness::last_supplied_count() const {
  return impl_->last_supplied_count();
}
bool FormApprovalVerticalHarness::CoreCommandsContain(
    std::string_view value) const {
  return impl_->CoreCommandsContain(value);
}

}  // namespace taffy::test
