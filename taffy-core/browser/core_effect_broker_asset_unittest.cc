// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "taffy/browser/core_effect_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::OperationEnvelopePtr Operation(uint64_t generation) {
  auto operation = mojom::OperationEnvelope::New();
  operation->operation_id = "operation-1";
  operation->service_generation = generation;
  operation->task_revision = 2;
  operation->deadline_monotonic_ms = 10'000;
  operation->idempotency_key = "idempotency-1";
  return operation;
}

mojom::EffectEnvelopePtr AssetFetchEffect(uint64_t generation) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = Operation(generation);
  effect->effect_id = "effect-asset-fetch";
  effect->kind = mojom::EffectKind::kDeliverAsset;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->asset_delivery = mojom::AssetDeliveryEffect::New();
  effect->asset_delivery->operation_kind =
      mojom::AssetDeliveryOperation::kFetchAsset;
  effect->asset_delivery->fetch = mojom::AssetFetchRequest::New();
  effect->asset_delivery->fetch->asset_id = "python-stdlib";
  effect->asset_delivery->fetch->asset_revision = "3.14.7-taffy.1";
  effect->asset_delivery->fetch->origin_path = "python/stdlib.zip";
  effect->asset_delivery->fetch->offset_bytes = 0u;
  effect->asset_delivery->fetch->total_bytes = 2'769'014u;
  // See StorageEffect above for why a bytes32 field is filled by hand: it
  // arrives empty from New() and the count is the contract's, not the type's.
  effect->asset_delivery->fetch->expected_digest.assign(32u, 7u);
  effect->asset_delivery->fetch->container = mojom::AssetContainer::kZip;
  return effect;
}

// The terminal for a refused transfer has to be a message that can actually be
// sent. `observed_digest` is `array<uint8, 32>`, and mojo checks the element
// count of a fixed-size array while it serialises the outgoing message: a
// terminal that left the field default-constructed did not fail validation at
// the core, it aborted the browser process. That is what took the whole
// application down on the first country screen, because the flag pack is
// requested there and a refused request answers through exactly this path.
TEST(CoreEffectBrokerTest, RefusedAssetTransferTerminalCarriesAWholeDigest) {
  CoreEffectBroker::Handlers handlers;
  handlers.commit_intent =
      base::BindLambdaForTesting([](const mojom::EffectEnvelope &,
                                    CoreEffectBroker::JournalCallback done) {
        std::move(done).Run(true);
      });
  CoreEffectBroker broker(std::move(handlers));
  broker.SetActiveGeneration(19);

  mojom::EffectResultPtr terminal;
  broker.Dispatch(
      AssetFetchEffect(19),
      base::BindLambdaForTesting([&](mojom::EffectResultPtr result) {
        terminal = std::move(result);
      }));

  ASSERT_TRUE(terminal);
  EXPECT_EQ(mojom::EffectStatus::kUnavailable, terminal->status);
  ASSERT_TRUE(terminal->asset_delivery);
  EXPECT_EQ(mojom::AssetDeliveryOperation::kFetchAsset,
            terminal->asset_delivery->operation_kind);
  ASSERT_TRUE(terminal->asset_delivery->transfer);
  const mojom::AssetTransferReport &transfer =
      *terminal->asset_delivery->transfer;
  EXPECT_EQ("python-stdlib", transfer.asset_id);
  EXPECT_EQ(mojom::AssetTransferOutcome::kInterrupted, transfer.outcome);
  EXPECT_EQ(32u, transfer.observed_digest.size());
  EXPECT_EQ(std::vector<uint8_t>(32u, 0u), transfer.observed_digest);
  // Nothing was read, so the bytes the report observed are the bytes the
  // request already had on disk rather than a length nobody measured.
  EXPECT_EQ(0u, transfer.observed_bytes);
}

} // namespace
} // namespace taffy
