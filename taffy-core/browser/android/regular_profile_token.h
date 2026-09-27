// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_REGULAR_PROFILE_TOKEN_H_
#define TAFFY_BROWSER_ANDROID_REGULAR_PROFILE_TOKEN_H_

#include <string>
#include <string_view>

namespace taffy {

// Mirrors opaqueProfileToken(Profile, false) without exposing the serialized
// Chromium ProfileToken outside the native browser process.
std::string OpaqueRegularProfileToken(std::string_view relative_path);

bool IsValidOpaqueRegularProfileToken(std::string_view token);

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_REGULAR_PROFILE_TOKEN_H_
