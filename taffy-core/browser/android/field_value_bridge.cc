// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include "chrome/browser/profiles/profile.h"
#include "taffy/browser/android/field_value_jni_headers/TaffyFieldValueBridge_jni.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/field_values/field_value_surface.mojom.h"

namespace taffy {

static jlong JNI_TaffyFieldValueBridge_Connect(JNIEnv* env, Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return 0;
  }
  CoreServiceManager* manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  if (!manager) {
    return 0;
  }
  mojo::PendingRemote<browser::field_values::mojom::TaffyFieldValueSurface>
      remote;
  manager->BindFieldValueSurface(remote.InitWithNewPipeAndPassReceiver());
  return remote.PassPipe().release().value();
}

DEFINE_JNI(TaffyFieldValueBridge)

}  // namespace taffy
