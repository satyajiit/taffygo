// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_auth_wire.h"

#include <stdint.h>

#include <algorithm>
#include <vector>

#include "base/base64url.h"
#include "base/containers/span.h"
#include "base/strings/escape.h"
#include "crypto/random.h"
#include "crypto/secure_util.h"
#include "crypto/sha2.h"

namespace taffy::provider_auth {

std::string RandomBase64Url(size_t byte_count) {
  std::vector<uint8_t> entropy(byte_count);
  crypto::RandBytes(entropy);
  std::string encoded;
  base::Base64UrlEncode(std::string(entropy.begin(), entropy.end()),
                        base::Base64UrlEncodePolicy::OMIT_PADDING, &encoded);
  std::ranges::fill(entropy, 0u);
  return encoded;
}

std::string PkceChallenge(const std::string &verifier) {
  std::string challenge;
  base::Base64UrlEncode(crypto::SHA256HashString(verifier),
                        base::Base64UrlEncodePolicy::OMIT_PADDING, &challenge);
  return challenge;
}

bool ConstantTimeEquals(const std::string &left, const std::string &right) {
  return left.size() == right.size() && !left.empty() &&
         crypto::SecureMemEqual(base::as_byte_span(left),
                                base::as_byte_span(right));
}

std::string FormField(const std::string &key, const std::string &value) {
  return key + "=" + base::EscapeUrlEncodedData(value, /*use_plus=*/true);
}

}  // namespace taffy::provider_auth
