// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_service_impl_task_effect_test_peer.h"

namespace taffy {
namespace core_service_impl_task_effect_test {

CoreServiceImplTaskEffectTest::CoreServiceImplTaskEffectTest()
    : impl_(service_.BindNewPipeAndPassReceiver()) {
  CoreServiceImplTaskEffectTestPeer::MakeReady(impl_, kGeneration,
                                               host_.BindNewPipe());
  CoreServiceImplTaskEffectTestPeer::StagePolicyEffect(impl_, kGeneration,
                                                       kTaskRevision);
}

CoreServiceImplTaskEffectTest::~CoreServiceImplTaskEffectTest() = default;

}  // namespace core_service_impl_task_effect_test
}  // namespace taffy
