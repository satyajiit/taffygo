// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/test/recovery/form_approval_vertical_test_internal.h"

namespace taffy::test {

surface::FieldValueSupplyVerdict FormApprovalVerticalHarness::Impl::Supply(
    std::vector<std::string> values) {
  surface::FieldValueSupplyVerdict verdict =
      surface::FieldValueSupplyVerdict::kMalformed;
  base::RunLoop loop;
  surface_->Supply(
      request_id_, std::move(values),
      base::BindOnce(
          [](surface::FieldValueSupplyVerdict* output, base::RunLoop* loop,
             surface::FieldValueSupplyVerdict result,
             std::vector<surface::FieldValueRefusalPtr> refused) {
            *output = result;
            static_cast<void>(refused);
            loop->Quit();
          },
          &verdict, &loop));
  loop.Run();
  return verdict;
}

bool FormApprovalVerticalHarness::Impl::WaitForSuppliedCount(
    uint32_t expected_count) const {
  return base::test::RunUntil([this, expected_count]() {
    return std::any_of(
        session_.commands().begin(), session_.commands().end(),
        [this, expected_count](const service::CoreServiceCommandPtr& command) {
          return command && command->supply_field_values &&
                 command->supply_field_values->task_id == task_id_ &&
                 command->supply_field_values->request_id == request_id_ &&
                 command->supply_field_values->supplied == expected_count;
        });
  });
}

bool FormApprovalVerticalHarness::Impl::ConfirmFillProposal(
    std::string action_id,
    std::string proposal_digest,
    uint32_t supplied_value_index) {
  action_id_ = std::move(action_id);
  proposal_digest_ = std::move(proposal_digest);
  supplied_value_index_ = supplied_value_index;
  if (!RegisterAndPublish(
          MakeTaskFormBindings(manager_->service_generation(), 4u, task_id_, 1u,
                               *start_command_->start_task, action_id_,
                               proposal_digest_, nullptr),
          4u)) {
    return false;
  }

  base::test::TestFuture<service::TaskEffectCompletionPtr> future;
  CoreServiceManagerTaskEffectTestPeer::Execute(
      *manager_,
      MakeFormApprovalEffect(
          manager_->service_generation(), 1u, task_id_, action_id_,
          proposal_digest_,
          MakeFillExecutable(target_, request_id_, supplied_value_index_),
          FormTestMonotonicMillis()),
      future.GetCallback());
  service::TaskEffectCompletionPtr completion = future.Take();
  if (!completion ||
      completion->status != service::TaskEffectCompletionStatus::kSucceeded) {
    return false;
  }
  // Succeeded means the browser owns the exact decision; it does not mean the
  // decision was admitted. A newer public state carrying the unchanged
  // approval binding proves the RequestApproval effect was acknowledged and
  // releases that command without changing its task revision.
  if (!RegisterAndPublish(
          MakeTaskFormBindings(manager_->service_generation(), 5u, task_id_, 1u,
                               *start_command_->start_task, action_id_,
                               proposal_digest_, nullptr),
          5u) ||
      !base::test::RunUntil([this]() {
        return std::any_of(
            session_.commands().begin(), session_.commands().end(),
            [this](const service::CoreServiceCommandPtr& command) {
              return command && command->user_decision &&
                     command->user_decision->task_id == task_id_ &&
                     command->user_decision->action_id == action_id_;
            });
      })) {
    return false;
  }
  for (auto it = session_.commands().rbegin(); it != session_.commands().rend();
       ++it) {
    if (*it && (*it)->user_decision &&
        (*it)->user_decision->task_id == task_id_ &&
        (*it)->user_decision->action_id == action_id_) {
      decision_command_ = (*it).Clone();
      break;
    }
  }
  if (!decision_command_ || !decision_command_->user_decision ||
      decision_command_->user_decision->decision !=
          service::UserDecisionKind::kAccept ||
      decision_command_->user_decision->approval_digest != proposal_digest_ ||
      !CoreServiceManagerTaskEffectTestPeer::CompleteStagedStorageCommit(
          *manager_, *decision_command_, task_id_, 2u,
          FormTestMonotonicMillis())) {
    return false;
  }
  return RegisterAndPublish(
      MakeTaskFormBindings(manager_->service_generation(), 6u, task_id_, 2u,
                           *start_command_->start_task, std::nullopt,
                           std::nullopt,
                           decision_command_->user_decision.get()),
      6u);
}

service::PolicyEvaluationStatus
FormApprovalVerticalHarness::Impl::AuthorizeFill() {
  if (!decision_command_ || !decision_command_->user_decision) {
    return service::PolicyEvaluationStatus::kInvalidRequest;
  }
  base::test::TestFuture<service::PolicyEvaluationResultPtr> future;
  CoreServiceManagerTaskEffectTestPeer::EvaluatePolicy(
      *manager_,
      MakeFormPolicyEffect(
          manager_->service_generation(), 2u, task_id_, action_id_,
          proposal_digest_, request_id_, supplied_value_index_, target_,
          *decision_command_->user_decision, FormTestMonotonicMillis()),
      future.GetCallback());
  service::PolicyEvaluationResultPtr result = future.Take();
  if (!result) {
    return service::PolicyEvaluationStatus::kInvalidRequest;
  }
  const service::PolicyEvaluationStatus status = result->status;
  if (status == service::PolicyEvaluationStatus::kGranted &&
      result->minted_grant) {
    grant_ = result->minted_grant.Clone();
  }
  return status;
}

service::TaskEffectCompletionStatus
FormApprovalVerticalHarness::Impl::DispatchFill() {
  if (!grant_) {
    return service::TaskEffectCompletionStatus::kRefused;
  }
  dispatch_ = MakeFormDispatchEffect(
      manager_->service_generation(), 2u, task_id_, action_id_,
      proposal_digest_, request_id_, supplied_value_index_, target_, *grant_);
  return ExecuteDispatch(dispatch_.Clone());
}

service::TaskEffectCompletionStatus
FormApprovalVerticalHarness::Impl::ReplayDispatch() {
  return dispatch_ ? ExecuteDispatch(dispatch_.Clone())
                   : service::TaskEffectCompletionStatus::kRefused;
}

service::TaskEffectCompletionStatus
FormApprovalVerticalHarness::Impl::ExecuteDispatch(
    service::TaskEffectBindingPtr effect) {
  base::test::TestFuture<service::TaskEffectCompletionPtr> future;
  CoreServiceManagerTaskEffectTestPeer::Execute(*manager_, std::move(effect),
                                                future.GetCallback());
  service::TaskEffectCompletionPtr completion = future.Take();
  return completion ? completion->status
                    : service::TaskEffectCompletionStatus::kUnavailable;
}

size_t FormApprovalVerticalHarness::Impl::submitted_command_count() const {
  return session_.commands().size();
}

uint32_t FormApprovalVerticalHarness::Impl::last_supplied_count() const {
  for (auto it = session_.commands().rbegin(); it != session_.commands().rend();
       ++it) {
    if (*it && (*it)->supply_field_values) {
      return (*it)->supply_field_values->supplied;
    }
  }
  return 0u;
}

bool FormApprovalVerticalHarness::Impl::CoreCommandsContain(
    std::string_view value) const {
  return std::any_of(session_.commands().begin(), session_.commands().end(),
                     [value](const service::CoreServiceCommandPtr& command) {
                       return command && FormCommandContains(*command, value);
                     });
}

}  // namespace taffy::test
