// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"

// The contract encoding, which is what the semantic graph travels as once it
// leaves the browser process for the isolated Rust core.
//
// Browser-process C++ never walks it — that is the Rule of Two answer, and the
// reason decision 0004 puts the core service in Rust — so what this target
// covers is the boundary before that: whether an arbitrary payload can be
// handled without the browser process interpreting it. The seed corpus is the
// contract's own golden documents, which are already in this encoding.
//
// The Rust decoder itself is fuzzed on its own side of the bridge, in
// the BIP contract's Rust crate. This target exists so that the C++ half of the
// boundary is covered by something, rather than by an argument that it does not
// parse.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  // Reading it as JSON is what a diagnostic tool would do with a payload, and
  // it is the one thing the browser side is ever tempted to do. Proving it is
  // survivable is worth more than proving it never happens.
  base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  return 0;
}
