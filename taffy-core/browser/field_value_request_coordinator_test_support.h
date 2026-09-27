// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_TEST_SUPPORT_H_
#define TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_TEST_SUPPORT_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/field_value_request_coordinator.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {
class BrowserTaskEnvironment;
}

namespace taffy::field_value_request_coordinator_test {

namespace surface_mojom = browser::field_values::mojom;

inline constexpr char kRequestId[] = "turn-4-values-1";
inline constexpr char kTaskId[] = "task-1";
inline constexpr char kTabId[] = "tab-1";
inline constexpr char kNodeId[] = "node-7";

ResolvedNodeFacts EditableField(std::string node_id, Sensitivity sensitivity);
ResolvedNodeFacts EditableField(Sensitivity sensitivity);

class RecordingClient final : public surface_mojom::TaffyFieldValueClient {
 public:
  RecordingClient();
  ~RecordingClient() override;

  void Open(surface_mojom::FieldValueRequestPtr request) override;
  void Close(const std::string& request_id,
             surface_mojom::FieldValueCloseReason reason) override;
  mojo::PendingRemote<surface_mojom::TaffyFieldValueClient> Bind();

  std::vector<surface_mojom::FieldValueRequestPtr> opened;
  std::vector<std::pair<std::string, surface_mojom::FieldValueCloseReason>>
      closed;
  mojo::Receiver<surface_mojom::TaffyFieldValueClient> receiver{this};
};

struct SupplyOutcome {
  bool answered = false;
  surface_mojom::FieldValueSupplyVerdict verdict =
      surface_mojom::FieldValueSupplyVerdict::kMalformed;
  std::vector<surface_mojom::FieldValueRefusalPtr> refused;
};

class FieldValueRequestCoordinatorTest : public testing::Test {
 protected:
  FieldValueRequestCoordinatorTest();
  ~FieldValueRequestCoordinatorTest() override;

  void SetUp() override;
  void Build();
  void OpenRequest();
  SupplyOutcome Supply(std::vector<std::string> values);

  std::unique_ptr<content::BrowserTaskEnvironment> task_environment_;
  ValueReferenceVault vault_;
  RecordingClient client_;
  Sensitivity sensitivity_ = Sensitivity::kIdentity;
  ChallengeKind challenge_kind_ = ChallengeKind::kNone;
  std::optional<ChallengeBounds> challenge_bounds_;
  std::map<std::string, ResolvedNodeFacts> node_facts_;
  bool challenge_presentation_available_ = true;
  // The clause the fake capture refuses under when it produces no
  // presentation. `kFieldChallengeRefusedOffScreen` is the one the coordinator
  // folds into `kChallengeOffScreen`; anything else folds into
  // `kCannotBeShown` (decision 0215).
  const char* challenge_refusal_ = "no-surface-bitmap";
  bool hold_challenge_completion_ = false;
  bool challenge_cancelled_ = false;
  int challenge_presentations_ = 0;
  FieldChallengePresentationCallback pending_challenge_completion_;
  bool node_gone_ = false;
  int resolves_ = 0;
  std::vector<std::pair<std::string, uint32_t>> reported_;
  std::vector<
      std::pair<std::string, core_service::mojom::FieldValueAskOutcome>>
      reported_outcomes_;
  // The field each held value was reported for, per request (decision 0238).
  std::vector<std::pair<std::string, std::vector<std::string>>>
      reported_field_node_ids_;
  std::vector<std::string> closed_request_ids_;
  // The report and close sinks in the order they ran, because the manager
  // admits a count only while the request is still open.
  std::vector<std::string> events_;
  std::string page_epoch_ = "page-1";
  std::unique_ptr<FieldValueRequestCoordinator> coordinator_;
};

}  // namespace taffy::field_value_request_coordinator_test

#endif  // TAFFY_BROWSER_FIELD_VALUE_REQUEST_COORDINATOR_TEST_SUPPORT_H_
