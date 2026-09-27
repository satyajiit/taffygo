// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_SUPPORT_H_
#define TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_SUPPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/browser/field_values/field_value_surface.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class WebContents;
}

namespace taffy {

class CoreServiceManager;

namespace test {

// Exact renderer-issued identities recovered from one bounded fixture graph.
// The test never guesses a DOM id from fixture source or page order.
struct LiveFormApprovalTarget {
  std::string tab_id;
  std::string form_node_id;
  std::string field_node_id;
  std::string frame_id;
  std::string page_epoch;
  std::string normalized_origin;
  uint64_t graph_revision = 0u;
};

// Drives a regular Profile through the production manager while a small
// scripted CoreSession supplies only deterministic reducer/policy responses.
// Browser authority, renderer observation, value custody, action dispatch,
// postcondition verification, and durable action journalling remain real.
class FormApprovalVerticalHarness final {
 public:
  FormApprovalVerticalHarness(CoreServiceManager* manager,
                              content::WebContents* web_contents);
  FormApprovalVerticalHarness(const FormApprovalVerticalHarness&) = delete;
  FormApprovalVerticalHarness& operator=(
      const FormApprovalVerticalHarness&) = delete;
  ~FormApprovalVerticalHarness();

  bool Initialize();
  bool DiscoverForm();
  bool StartWebErrand(std::string source_host);
  bool OpenFieldValueRequest(std::string request_id);

  const LiveFormApprovalTarget& target() const;
  const std::string& task_id() const;
  const std::string& request_id() const;
  const std::vector<std::string>& field_ids() const;
  const std::vector<std::string>& field_labels() const;
  uint32_t approval_lifetime_seconds() const;

  browser::field_values::mojom::FieldValueSupplyVerdict Supply(
      std::vector<std::string> values);
  bool WaitForSuppliedCount(uint32_t expected_count);
  bool ConfirmFillProposal(std::string action_id,
                           std::string proposal_digest,
                           uint32_t supplied_value_index);
  core_service::mojom::PolicyEvaluationStatus AuthorizeFill();
  core_service::mojom::TaskEffectCompletionStatus DispatchFill();
  core_service::mojom::TaskEffectCompletionStatus ReplayDispatch();

  size_t submitted_command_count() const;
  uint32_t last_supplied_count() const;
  bool CoreCommandsContain(std::string_view value) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace test
}  // namespace taffy

#endif  // TAFFY_TEST_RECOVERY_FORM_APPROVAL_VERTICAL_TEST_SUPPORT_H_
