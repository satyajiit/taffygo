// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_model_registration.h"

#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::core_service_internal {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

bridge::BridgeModelArtifactRegistration Registration() {
  bridge::BridgeModelArtifactRegistration row{};
  row.asset_id = "model.small";
  row.asset_revision = "2026-08-01";
  row.asset_kind = 2u;  // MODEL_WEIGHTS
  row.format = 2u;      // GGUF
  row.byte_length = 1024u;
  row.digest[0] = 0xa5u;
  return row;
}

TEST(RustCoreModelRegistrationTest, ExactTypedRowProjectsEveryFact) {
  rust::Vec<bridge::BridgeModelArtifactRegistration> rows;
  rows.push_back(Registration());
  auto state = mojom::CoreStateUpdate::New();
  ASSERT_TRUE(PopulateModelArtifactRegistrations(rows, state.get()));
  ASSERT_EQ(state->model_artifacts.size(), 1u);
  const auto& projected = state->model_artifacts.front();
  EXPECT_EQ(projected->asset_id, "model.small");
  EXPECT_EQ(projected->asset_revision, "2026-08-01");
  EXPECT_EQ(projected->asset_kind, mojom::AssetKind::kModelWeights);
  EXPECT_EQ(projected->format, mojom::ToolModelArtifactKind::kGguf);
  EXPECT_FALSE(projected->adapter);
  EXPECT_EQ(projected->byte_length, 1024u);
  ASSERT_EQ(projected->digest.size(), 32u);
  EXPECT_EQ(projected->digest.front(), 0xa5u);
}

TEST(RustCoreModelRegistrationTest, UnknownKindOrFormatFailsClosed) {
  auto state = mojom::CoreStateUpdate::New();
  rust::Vec<bridge::BridgeModelArtifactRegistration> rows;
  bridge::BridgeModelArtifactRegistration unknown_kind = Registration();
  unknown_kind.asset_kind = 6u;
  rows.push_back(std::move(unknown_kind));
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));

  rows.clear();
  bridge::BridgeModelArtifactRegistration unknown_format = Registration();
  unknown_format.format = 3u;
  rows.push_back(std::move(unknown_format));
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));
}

TEST(RustCoreModelRegistrationTest, TokenizerAdapterAndDuplicateFailClosed) {
  auto state = mojom::CoreStateUpdate::New();
  rust::Vec<bridge::BridgeModelArtifactRegistration> rows;
  bridge::BridgeModelArtifactRegistration tokenizer = Registration();
  tokenizer.asset_kind = 3u;  // MODEL_TOKENIZER
  tokenizer.adapter = true;
  rows.push_back(std::move(tokenizer));
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));

  rows.clear();
  rows.push_back(Registration());
  rows.push_back(Registration());
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));
}

TEST(RustCoreModelRegistrationTest, ZeroAndOversizedArtifactsFailClosed) {
  auto state = mojom::CoreStateUpdate::New();
  rust::Vec<bridge::BridgeModelArtifactRegistration> rows;
  bridge::BridgeModelArtifactRegistration zero = Registration();
  zero.byte_length = 0u;
  rows.push_back(std::move(zero));
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));

  rows.clear();
  bridge::BridgeModelArtifactRegistration oversized = Registration();
  oversized.byte_length = mojom::kMaxToolModelArtifactBytes + 1u;
  rows.push_back(std::move(oversized));
  EXPECT_FALSE(PopulateModelArtifactRegistrations(rows, state.get()));
}

}  // namespace
}  // namespace taffy::core_service_internal
