// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/test/browser_task_environment.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "services/network/test/test_utils.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_model_broker_test_support.h"
#include "taffy/browser/profile_page_media_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using model_broker_test::kCredential;
using model_broker_test::kGeneration;
using model_broker_test::ModelEffect;

class ProfileModelBrokerMediaTest : public testing::Test {
 protected:
  static ProfilePageMediaStore::StoreRequest StoreRequest() {
    return ProfilePageMediaStore::StoreRequest{
        .task_id = model_broker_test::kTaskId,
        .observation_effect_id = "observation-1",
        .service_generation = kGeneration,
        .tab_id = "tab-1",
        .frame_id = "frame-1",
        .page_epoch = "epoch-1",
        .graph_revision = 3u,
        .node_id = "node-1",
        .width_px = 2u,
        .height_px = 2u,
    };
  }

  static std::vector<uint8_t> Png() {
    return {
        0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au, 0x00u,
        0x00u, 0x00u, 0x0du, 'I',   'H',   'D',   'R',   0x00u, 0x00u,
        0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x02u, 0x08u, 0x06u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    };
  }

  void SetUp() override {
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
    broker_ = std::make_unique<ProfileModelBroker>(
        shared_factory_, std::string(model_broker_test::kWorkerOrigin));
    broker_->SetCredentialResolver(base::BindRepeating(
        [](const std::string&, const std::string&,
           ProfileModelBroker::ProviderCredentialCallback callback) {
          std::move(callback).Run(std::string(kCredential), std::nullopt);
        }));
    broker_->SetModelStreamChunkDispatcher(base::BindRepeating(
        [](mojom::ModelStreamChunkPtr,
           ProfileModelBroker::ModelStreamChunkCallback callback) {
          std::move(callback).Run(mojom::ModelStreamChunkStatus::kAccepted);
        }));
    broker_->SetPageMediaStore(store_);
    factory_.SetInterceptor(base::BindRepeating(
        [](std::string* body, const network::ResourceRequest& request) {
          *body = network::GetUploadData(request);
        },
        &observed_body_));
  }

  mojom::EffectResultPtr Run(mojom::EffectEnvelopePtr effect) {
    mojom::EffectResultPtr result;
    broker_->Dispatch(std::move(effect), base::BindOnce(
                                             [](mojom::EffectResultPtr* out,
                                                mojom::EffectResultPtr value) {
                                               *out = std::move(value);
                                             },
                                             &result));
    task_environment_.RunUntilIdle();
    return result;
  }

  mojom::EffectEnvelopePtr MediaEffect(const std::string& handle,
                                       std::string body) {
    mojom::EffectEnvelopePtr effect = ModelEffect();
    effect->model_request->disclosure = mojom::DisclosureClass::kPageContent;
    effect->model_request->media_attachment_handle = handle;
    effect->model_request->media_attachment_mime_type = "image/png";
    effect->model_request->request_body.assign(body.begin(), body.end());
    return effect;
  }

  content::BrowserTaskEnvironment task_environment_;
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
  scoped_refptr<ProfilePageMediaStore> store_ =
      base::MakeRefCounted<ProfilePageMediaStore>(base::BindRepeating(
          [](const ProfilePageMediaStore::StoreRequest&) { return true; }));
  std::unique_ptr<ProfileModelBroker> broker_;
  std::string observed_body_;
};

TEST_F(ProfileModelBrokerMediaTest,
       ExactHandleIsClaimedAndReplacedWithImageBytesOnce) {
  const std::vector<uint8_t> png = Png();
  const auto stored =
      store_->StoreRenderedPng(StoreRequest(), Png(), base::TimeTicks::Now());
  ASSERT_TRUE(stored);
  factory_.AddResponse("https://provider.taffy.test/v1/messages", "{}");

  mojom::EffectResultPtr result = Run(
      MediaEffect(stored->handle, "{\"image\":\"" + stored->handle + "\"}"));

  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::EffectStatus::kCompleted);
  EXPECT_EQ(observed_body_,
            "{\"image\":\"" + base::Base64Encode(base::span(png)) + "\"}");
  EXPECT_EQ(store_->entry_count_for_testing(), 0u);
}

TEST_F(ProfileModelBrokerMediaTest,
       MissingOrRepeatedBodyHandleBurnsTheClaimAndSendsNothing) {
  for (bool repeated : {false, true}) {
    const auto stored =
        store_->StoreRenderedPng(StoreRequest(), Png(), base::TimeTicks::Now());
    ASSERT_TRUE(stored);
    const std::string body =
        repeated ? stored->handle + ":" + stored->handle : "no marker";

    mojom::EffectResultPtr result = Run(MediaEffect(stored->handle, body));

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, mojom::EffectStatus::kInvalidResult);
    EXPECT_EQ(factory_.NumPending(), 0);
    EXPECT_EQ(store_->entry_count_for_testing(), 0u);
  }
}

}  // namespace
}  // namespace taffy
