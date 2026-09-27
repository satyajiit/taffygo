// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "base/check.h"
#include "base/check_op.h"

#include <string_view>

#include "taffy/components/intelligence/content/secret_shape_scanner.h"

// The pattern detector that finds secret-shaped values in text that was never
// classified as a field. It runs over arbitrary page-derived text, so its
// inputs are entirely adversarial, and it has to be total: every input returns,
// and no input makes a match that runs past the end of the text it was given.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  for (const taffy::SecretMatch& match : taffy::ScanForSecretShapes(text)) {
    CHECK_LE(match.begin, match.end);
    CHECK_LE(match.end, text.size());
    if (match.length() > 0) {
      taffy::PassesLuhnCheck(text.substr(match.begin, match.length()));
    }
  }
  return 0;
}
