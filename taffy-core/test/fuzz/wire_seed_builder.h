// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_FUZZ_WIRE_SEED_BUILDER_H_
#define TAFFY_TEST_FUZZ_WIRE_SEED_BUILDER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/values.h"

// Turns a contract golden document into the wire bytes a browser-facing
// renderer payload actually arrives as.
//
// A fuzzer's seed corpus has to be in the encoding the target decodes. The
// contract's golden documents are JSON, which is the right seed for the graph
// payload — that encoding is generated from the contract — and the wrong seed
// for a Mojo struct, which decodes a binary layout. Handing a Mojo decoder a
// directory of JSON would look like a seed corpus and would be worth nothing:
// every seed would be rejected in the first bytes and the fuzzer would start
// from nothing.
//
// So this builder reads a golden document and produces the serialized Mojo
// bytes of the corresponding message. The result is a seed corpus genuinely
// derived from the contract, message for message.
//
// **It fails rather than skips.** Build() returns nothing for a definition it
// does not know how to build, and the writer that drives it treats that as an
// error unless the definition appears in its table of messages that never cross
// this boundary. A golden document added later therefore either gains a seed or
// stops the build; it cannot quietly stop being covered.

namespace taffy::test {

class WireSeedBuilder {
 public:
  WireSeedBuilder() = delete;

  // The serialized message for one golden document, or nothing when
  // `definition` names a message this boundary never carries.
  //
  // `definition` is the contract definition name from
  // taffy-core/contracts/bip/golden/index.json, for example "PageSnapshot".
  static std::optional<std::vector<uint8_t>> Build(
      const std::string& definition,
      const base::DictValue& document);

  // True when `definition` is a message the browser receives from a renderer,
  // and therefore one this boundary has to be fuzzed against.
  static bool IsBrowserFacingRendererPayload(const std::string& definition);

  // Every definition this builder can produce, for the writer's own
  // completeness check.
  static std::vector<std::string> SupportedDefinitions();
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_FUZZ_WIRE_SEED_BUILDER_H_
