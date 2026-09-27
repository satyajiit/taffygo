// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/page_inspector_projection_mapper.h"

#include <string>
#include <utility>

#include "taffy/common/public/bip_observation.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

ObservationEnvelope ValidObservation() {
  ObservationEnvelope observation;
  observation.code = ObservationResultCode::kOk;
  observation.graph_revision = 9u;
  observation.lifecycle_state = DocumentLifecycleState::kActive;
  observation.origin = Origin{
      .kind = OriginKind::kTuple,
      .serialization = "https://shop.example.test:8443/private/path?token=no",
  };
  observation.is_potentially_trustworthy = true;
  observation.adapters.push_back(AdapterReport{
      .adapter = AdapterKind::kDom,
      .status = AdapterStatus::kOk,
      .adapter_version = 2u,
  });
  observation.frames.push_back(FrameSummary{
      .is_main_frame = true,
      .lifecycle_state = DocumentLifecycleState::kActive,
      .included = true,
  });
  InspectorGraphProjection projection;
  projection.nodes.push_back(InspectorNodeProjection{
      .display_id = "item-1",
      .role = InspectorNodeRole::kSection,
      .name = "Checkout",
      .sensitivity = InspectorSensitivity::kPublic,
      .text_run_count = 1u,
      .text_byte_count = 8u,
  });
  observation.inspector_projection = std::move(projection);
  return observation;
}

TEST(PageInspectorProjectionMapperTest, ExposesHostButNeverTheApprovedUrl) {
  auto projected = ProjectPageInspectorObservation(ValidObservation());

  ASSERT_TRUE(projected);
  EXPECT_EQ("shop.example.test", projected->host);
  EXPECT_EQ("selected-page", projected->document_id);
  ASSERT_EQ(1u, projected->nodes.size());
  EXPECT_EQ("item-1", projected->nodes[0]->display_id);
  EXPECT_EQ(std::string::npos, projected->host.find("private"));
  EXPECT_EQ(std::string::npos, projected->host.find("token"));
}

TEST(PageInspectorProjectionMapperTest, RefusesOversizedProjectedText) {
  ObservationEnvelope observation = ValidObservation();
  ASSERT_TRUE(observation.inspector_projection);
  ASSERT_TRUE(observation.inspector_projection->nodes[0].name);
  observation.inspector_projection->nodes[0].name =
      std::string(core_api::mojom::kMaxPageInspectorNameBytes + 1u, 'x');

  EXPECT_FALSE(ProjectPageInspectorObservation(observation));
}

TEST(PageInspectorProjectionMapperTest, RefusesChildFrameProjection) {
  ObservationEnvelope observation = ValidObservation();
  observation.frames.push_back(FrameSummary{
      .is_main_frame = false,
      .lifecycle_state = DocumentLifecycleState::kActive,
      .included = true,
  });

  // Only one browser-owned frame may be represented by this direct contract.
  EXPECT_FALSE(ProjectPageInspectorObservation(observation));
}

}  // namespace
}  // namespace taffy
