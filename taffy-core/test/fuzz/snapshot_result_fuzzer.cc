// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stddef.h>
#include <stdint.h>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"

// The largest thing a renderer sends the browser.
//
// A snapshot result carries an unbounded node array, an unbounded frame array,
// nested optional structs and every enumeration in the contract. Mojo validates
// the layout and refuses anything outside a closed enumeration before this
// code runs; what is left afterwards — a structurally valid message whose
// values are hostile — is what the browser process has to survive, and this
// target is where that is measured.
//
// The body deliberately goes on to convert the parts the browser reads. A
// decoder that accepts a message and a consumer that walks it are two different
// attack surfaces, and stopping at Deserialize would leave the second untested.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  taffy::mojom::SnapshotResultPtr result;
  if (!taffy::mojom::SnapshotResult::Deserialize(data, size, &result)) {
    return 0;
  }
  if (!result || !result->snapshot) {
    return 0;
  }

  // Every field dereferenced below is non-nullable in the contract, so Mojo has
  // already refused a message that omits one; `origin` is a struct, so reaching
  // its converter needs the same dereference `truncation` gets.
  const taffy::mojom::PageSnapshot& snapshot = *result->snapshot;
  taffy::FromMojom(*snapshot.origin_metadata->origin);
  taffy::FromMojom(*snapshot.truncation);
  taffy::FromMojom(snapshot.lifecycle_state);
  for (const auto& frame : snapshot.frames) {
    taffy::FromMojom(*frame->origin_metadata->origin);
    taffy::FromMojom(frame->lifecycle_state);
  }
  for (const auto& report : snapshot.adapters) {
    taffy::FromMojom(report->adapter);
  }
  return 0;
}
