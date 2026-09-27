// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/sensitive_name_list.h"

#include <array>
#include <string>

#include "base/strings/string_util.h"

namespace taffy {

namespace {

// Lowercase; the comparison lowercases the input. Grouped by what they are so
// that a reviewer can see which classes of secret the name-based half claims
// to cover, and which it leaves to the shape rules.
//
// std::array rather than a C array because GetSensitiveValueName() subscripts
// this with a runtime index, and at 152.0.7977.42 //taffy compiles
// with -Wunsafe-buffer-usage, under which a C array decays to a raw pointer at
// the subscript and the bounds go with it. std::array carries its own size, so
// the same expression is checkable — see docs/unsafe_buffers.md, "Use of
// std::array<T>". No element or ordering changes: the corpus test walks this
// list by index and its expectations are the list.
constexpr auto kSensitiveNames = std::to_array<std::string_view>({
    // Passwords and their spellings in the wild.
    "password", "passwd", "pwd",

    // Bearer credentials.
    "token", "access_token", "id_token", "refresh_token", "auth",
    "authorization", "api_key", "apikey", "client_secret", "secret",
    "signature",

    // Authorization-flow parameters. "code" and "state" are the two that carry
    // an authorization grant, and they are also two of the most ordinary
    // English words there are — which is why over-redaction is the accepted
    // cost here (see the file comment in secret_shape_rules.cc).
    "code", "state",

    // Second factors and card data.
    "otp", "totp", "cvv", "cvc", "pin", "recovery_code",

    // Key material.
    "seed", "mnemonic", "private_key",

    // Session identity.
    "session", "sessionid", "session_id", "cookie",
});

}  // namespace

bool IsSensitiveValueName(std::string_view name) {
  const std::string lowered = base::ToLowerASCII(name);
  for (std::string_view candidate : kSensitiveNames) {
    if (lowered == candidate) {
      return true;
    }
  }
  return false;
}

size_t GetSensitiveValueNameCount() {
  return kSensitiveNames.size();
}

std::string_view GetSensitiveValueName(size_t index) {
  if (index >= GetSensitiveValueNameCount()) {
    return std::string_view();
  }
  return kSensitiveNames[index];
}

}  // namespace taffy
