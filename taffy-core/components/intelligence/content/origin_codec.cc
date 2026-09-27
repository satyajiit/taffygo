// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/origin_codec.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

// Bounds the opaque table so a session that visits many sandboxed documents
// cannot grow it without limit. Evicting an entry is safe by construction: the
// origin gets a new identifier, so every handle carrying the old one fails its
// origin check.
constexpr size_t kMaxTrackedOpaqueOrigins = 4096;

// A session-local prefix so an identifier from a previous session, recovered
// from a persisted diagnostic record, cannot be mistaken for a live one. It is
// not a secret and does not need to be: it exists to prevent confusion, not to
// prevent forgery. Forgery is already impossible because the isolated core cannot
// mint a capability.
constexpr char kOpaquePrefix[] = "opq_";

}  // namespace

// static
OriginCodec& OriginCodec::Get() {
  static base::NoDestructor<OriginCodec> instance;
  return *instance;
}

OriginCodec::OriginCodec() = default;
OriginCodec::~OriginCodec() = default;

Origin OriginCodec::ToWireOrigin(const url::Origin& origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  Origin out;
  if (!origin.opaque()) {
    out.kind = OriginKind::kTuple;
    out.serialization = origin.Serialize();
    return out;
  }

  out.kind = OriginKind::kOpaque;
  auto it = by_origin_.find(origin);
  if (it != by_origin_.end()) {
    out.opaque_id = it->second;
    return out;
  }

  EvictOldestIfNeeded();
  // url::Origin's map ordering and equality are nonce aware, so two distinct
  // opaque origins land on two distinct entries here.
  std::string opaque_id =
      base::StrCat({kOpaquePrefix, base::NumberToString(next_opaque_value_++)});
  by_origin_.emplace(origin, opaque_id);
  insertion_order_.push_back(origin);
  out.opaque_id = std::move(opaque_id);
  return out;
}

bool OriginCodec::Matches(const Origin& expected, const url::Origin& actual) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (expected.kind == OriginKind::kTuple) {
    return !actual.opaque() && !expected.serialization.empty() &&
           expected.serialization == actual.Serialize();
  }

  if (!actual.opaque() || expected.opaque_id.empty()) {
    return false;
  }
  auto it = by_origin_.find(actual);
  // A miss means the entry was evicted, or this opaque origin was never seen.
  // Either way the answer is no, and the caller turns that into
  // ActionResultCode::kOriginChanged.
  return it != by_origin_.end() && it->second == expected.opaque_id;
}

void OriginCodec::EvictOldestIfNeeded() {
  while (by_origin_.size() >= kMaxTrackedOpaqueOrigins &&
         !insertion_order_.empty()) {
    const url::Origin oldest = insertion_order_.front();
    insertion_order_.pop_front();
    by_origin_.erase(oldest);
  }
}

void OriginCodec::ClearForTesting() {
  by_origin_.clear();
  insertion_order_.clear();
  next_opaque_value_ = 1;
}

}  // namespace taffy
