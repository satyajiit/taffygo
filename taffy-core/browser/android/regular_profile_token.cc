// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/regular_profile_token.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "base/base64url.h"
#include "chrome/browser/android/proto/profile_token.pb.h"
#include "crypto/hash.h"

namespace taffy {

std::string OpaqueRegularProfileToken(std::string_view relative_path) {
  if (relative_path.empty()) {
    return {};
  }
  profile_resolver::ProfileToken token;
  token.set_relative_path(relative_path);
  std::string partitioned_token;
  if (!token.SerializeToString(&partitioned_token)) {
    return {};
  }
  partitioned_token.append(":regular");
  const std::array<uint8_t, crypto::hash::kSha256Size> digest =
      crypto::hash::Sha256(partitioned_token);
  std::string opaque_token;
  base::Base64UrlEncode(digest, base::Base64UrlEncodePolicy::OMIT_PADDING,
                        &opaque_token);
  return opaque_token;
}

bool IsValidOpaqueRegularProfileToken(std::string_view token) {
  return token.size() == 43 &&
         std::ranges::all_of(token, [](unsigned char character) {
           return (character >= 'a' && character <= 'z') ||
                  (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') || character == '_' ||
                  character == '-';
         });
}

}  // namespace taffy
