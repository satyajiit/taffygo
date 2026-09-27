// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <jni.h>

#include <string>

#include "base/android/jni_string.h"
#include "taffy/browser/android/attribution_notice_jni_headers/TaffyAttributionNoticeBridge_jni.h"
#include "taffy/common/public/taffy_product_identity.h"

namespace taffy {

// Answers the attribution page's address to About (decision 0206).
//
// It reads the product identity rather than holding a second copy of the
// address, and it takes nothing from its caller: the Java side has no way to
// ask for any other page through this entry point, which is what keeps decision
// 0154's refusal of engine pages intact everywhere else.
static std::string JNI_TaffyAttributionNoticeBridge_AttributionResourcePath(
    JNIEnv* env) {
  return GetProductIdentity().attribution_resource_path;
}

DEFINE_JNI(TaffyAttributionNoticeBridge)

}  // namespace taffy
