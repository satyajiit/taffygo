// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/profile_asset_plane.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/assets/asset_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// What this file is about: the shipped delivery origin is empty (decision
// 0202), and two comments in the tree say what that means — the argument's own
// comment in `asset_delivery_configuration.gni` and this class's header, which
// promises "an absent origin refuses every fetch and breaks nothing else".
//
// `asset_delivery_configuration_unittest.cc` already checks the two steps
// below this one: an empty origin validates as `kAbsent`, and `AssetUrl("",
// path)` is invalid. What nothing checked was the step those two exist for —
// that the plane turns an invalid URL into a refusal a person can read rather
// than into a request, a crash, or a callback that never runs.
//
// The second test is the control and is not optional. A suite that only
// asserted the refusal would pass just as well for a plane that refused every
// fetch under every origin, which is a measurement narrower than the claim it
// is quoted for.

constexpr char kOrigin[] = "https://assets.example";
constexpr char kPath[] = "python/3.14.2/stdlib-arm64.zip";
constexpr char kUrl[] = "https://assets.example/python/3.14.2/stdlib-arm64.zip";
constexpr char kAssetId[] = "python-stdlib";
constexpr char kRevision[] = "3.14.2";
constexpr std::string_view kArtifact = "an installed artifact's bytes";

class ProfileAssetPlaneTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
  }

  AssetStore Store() const { return AssetStore(directory_.GetPath()); }

  std::unique_ptr<ProfileAssetPlane> Plane(std::string origin) {
    return std::make_unique<ProfileAssetPlane>(
        shared_factory_, directory_.GetPath(), std::move(origin));
  }

  // The one effect every test here asks for: a well-formed fetch, so that a
  // refusal can only be about the origin.
  service::AssetDeliveryEffectPtr Fetch() {
    auto fetch = service::AssetFetchRequest::New();
    fetch->asset_id = kAssetId;
    fetch->asset_revision = kRevision;
    fetch->origin_path = kPath;
    fetch->offset_bytes = 0;
    fetch->total_bytes = kArtifact.size();
    fetch->expected_digest = std::vector<uint8_t>(32u, 0u);
    fetch->container = service::AssetContainer::kZip;

    auto effect = service::AssetDeliveryEffect::New();
    effect->operation_kind = service::AssetDeliveryOperation::kFetchAsset;
    effect->fetch = std::move(fetch);
    return effect;
  }

  // Performs one effect and answers what it reported. A callback that never
  // ran would hang here rather than pass, which is deliberate: "exactly once"
  // is part of `Perform`'s contract.
  service::AssetDeliveryEffectResultPtr Perform(
      ProfileAssetPlane& plane,
      service::AssetDeliveryEffectPtr effect) {
    service::AssetDeliveryEffectResultPtr reported;
    base::RunLoop loop;
    plane.Perform(std::move(effect),
                  base::BindLambdaForTesting(
                      [&](service::AssetDeliveryEffectResultPtr result) {
                        reported = std::move(result);
                        loop.Quit();
                      }));
    loop.Run();
    return reported;
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir directory_;
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
};

TEST_F(ProfileAssetPlaneTest, AnAbsentOriginRefusesTheFetchPermanently) {
  std::unique_ptr<ProfileAssetPlane> plane = Plane("");

  const service::AssetDeliveryEffectResultPtr result =
      Perform(*plane, Fetch());

  ASSERT_TRUE(result);
  EXPECT_EQ(result->operation_kind,
            service::AssetDeliveryOperation::kFetchAsset);
  ASSERT_TRUE(result->transfer);
  // Permanent rather than temporary: no amount of waiting configures an
  // origin, and a retryable answer would spend this artifact's attempts and
  // start a backoff nothing wakes.
  EXPECT_EQ(result->transfer->outcome,
            service::AssetTransferOutcome::kOriginRefusedPermanent);
  EXPECT_EQ(result->transfer->asset_id, kAssetId);
  EXPECT_EQ(result->transfer->asset_revision, kRevision);
  EXPECT_EQ(result->transfer->written_bytes, 0u);

  EXPECT_EQ(factory_.NumPending(), 0)
      << "an absent origin may not produce a request to anywhere";
  EXPECT_FALSE(plane->has_transfer());

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(Store().Scan().empty())
      << "a refused fetch may not leave a staged file behind";
}

TEST_F(ProfileAssetPlaneTest, AConfiguredOriginIsAskedForTheSameFetch) {
  // The control for the test above. Same effect, same plane, one argument
  // different — so the refusal there is about the origin and not about this
  // fetch or about a plane that refuses everything.
  std::unique_ptr<ProfileAssetPlane> plane = Plane(kOrigin);

  bool answered = false;
  plane->Perform(Fetch(),
                 base::BindLambdaForTesting(
                     [&](service::AssetDeliveryEffectResultPtr result) {
                       answered = true;
                     }));
  task_environment_.RunUntilIdle();

  EXPECT_TRUE(plane->has_transfer());
  EXPECT_TRUE(factory_.IsPending(kUrl))
      << "the catalog's path, resolved against the configured origin";
  EXPECT_FALSE(answered)
      << "a transfer that is still waiting on the origin has no outcome yet";
}

TEST_F(ProfileAssetPlaneTest, AnAbsentOriginStillRemovesWhatIsInstalled) {
  // The other half of the header's promise: "refuses every fetch and breaks
  // nothing else". A removal touches no origin, so an empty one must not
  // change it — and this is the path a person takes to reclaim the space a
  // bundled artifact occupies.
  {
    base::File staged = Store().OpenStaging(kAssetId, kRevision, 0);
    ASSERT_TRUE(staged.IsValid());
    ASSERT_TRUE(staged.WriteAtCurrentPosAndCheck(base::as_byte_span(kArtifact)));
  }
  ASSERT_TRUE(Store().Commit(kAssetId, kRevision));
  ASSERT_EQ(Store().Scan().size(), 1u);

  std::unique_ptr<ProfileAssetPlane> plane = Plane("");

  auto remove = service::AssetRemoveRequest::New();
  remove->asset_id = kAssetId;
  remove->asset_revision = kRevision;
  auto effect = service::AssetDeliveryEffect::New();
  effect->operation_kind = service::AssetDeliveryOperation::kRemoveAsset;
  effect->remove = std::move(remove);

  const service::AssetDeliveryEffectResultPtr result =
      Perform(*plane, std::move(effect));

  ASSERT_TRUE(result);
  EXPECT_EQ(result->operation_kind,
            service::AssetDeliveryOperation::kRemoveAsset);
  ASSERT_TRUE(result->removal);
  EXPECT_EQ(result->removal->reclaimed_bytes, kArtifact.size());
  EXPECT_TRUE(Store().Scan().empty());
}

}  // namespace
}  // namespace taffy
