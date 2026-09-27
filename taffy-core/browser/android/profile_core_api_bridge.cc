// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "taffy/browser/android/core_api_jni_headers/TaffyCoreApiBridge_jni.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy {

static jlong JNI_TaffyCoreApiBridge_Connect(JNIEnv *env, Profile *profile) {
  if (!profile) {
    return 0;
  }
  CoreServiceManager *manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  if (!manager) {
    return 0;
  }
  mojo::PendingRemote<core_api::mojom::TaffyProfileCoreApi> remote;
  mojo::MakeSelfOwnedReceiver(std::make_unique<ProfileCoreApiFacade>(manager),
                              remote.InitWithNewPipeAndPassReceiver());
  return remote.PassPipe().release().value();
}

DEFINE_JNI(TaffyCoreApiBridge)

} // namespace taffy
