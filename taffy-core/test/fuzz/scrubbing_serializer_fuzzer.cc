// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>

#include "taffy/components/intelligence/content/scrubbed_text.h"
#include "taffy/components/intelligence/content/scrubbing_serializer.h"
#include "url/gurl.h"

// The gate every diagnostic string passes through. Its inputs are page-derived,
// so they are adversarial by definition, and the property is absolute: what
// comes out is a scrubbed value, and nothing that goes in can produce anything
// else.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  taffy::ScrubbingSerializer::Serialize(text);
  taffy::ScrubbingSerializer::SerializeOriginOf(GURL(std::string(text)));
  return 0;
}
