// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ORIGIN_CODEC_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ORIGIN_CODEC_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

#include <map>

#include "base/containers/circular_deque.h"
#include "base/no_destructor.h"
#include "taffy/common/public/bip_identity.h"
#include "url/origin.h"

// Translates between url::Origin and the contract's Origin value type
// (taffy-core/contracts/bip/schema/identity.schema.json).
//
// A tuple origin needs no state: its serialization is deterministic, so
// converting either way is a pure function.
//
// An opaque origin is the reason this class exists. Every opaque origin
// serializes to the same thing, so if the isolated core held serialized origins, two
// unrelated sandboxed documents would compare equal and an action authorized
// against one would pass a check against the other. Protocol section 5.5
// requires the opposite: "opaque origins remain opaque; they are not broadened
// to a predecessor origin". So each distinct opaque url::Origin — distinct by
// nonce, which url::Origin equality already knows about — gets a session-local
// opaque_id, and comparison happens here on real url::Origin values.
//
// The opaque table is bounded. An opaque origin is minted per document, so an
// unbounded table would grow for the life of the session. When the bound is
// reached the oldest entries are evicted, and an evicted origin gets a new
// opaque_id next time it is seen, so any handle carrying the old one fails
// closed. That is the correct failure: a stale handle is meant to die.
//
// UI thread only.

namespace taffy {

class OriginCodec {
 public:
  static OriginCodec& Get();

  OriginCodec(const OriginCodec&) = delete;
  OriginCodec& operator=(const OriginCodec&) = delete;

  // The contract projection of a live Chromium origin.
  Origin ToWireOrigin(const url::Origin& origin);

  // True when `expected` names exactly `actual`. For a tuple origin this is a
  // serialization comparison; for an opaque origin it is a comparison of the
  // session-local identifier, which is unique per nonce.
  bool Matches(const Origin& expected, const url::Origin& actual);

  size_t opaque_entry_count_for_testing() const { return by_origin_.size(); }
  void ClearForTesting();

 private:
  friend base::NoDestructor<OriginCodec>;

  OriginCodec();
  ~OriginCodec();

  void EvictOldestIfNeeded();

  // Opaque origins only. Tuple origins are stateless.
  std::map<url::Origin, std::string> by_origin_;
  base::circular_deque<url::Origin> insertion_order_;
  uint64_t next_opaque_value_ = 1;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_ORIGIN_CODEC_H_
