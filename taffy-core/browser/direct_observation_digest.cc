// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/direct_observation_digest.h"

#include <string>
#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

void AppendDigestField(std::string_view value, std::string* output) {
  output->append(base::NumberToString(value.size()));
  output->push_back(':');
  output->append(value);
  output->push_back('|');
}

}  // namespace

DirectObservationDigestOperands FixedDirectObservationDigestOperands() {
  DirectObservationDigestOperands operands;
  // V3 adds destination flags to the document projection's existing action,
  // content-trust, content-signal, and state evidence. Exact link addresses
  // remain in browser-only transient storage. The projection roster is
  // implicit in this domain: adding fields without changing the domain would
  // assign the same proposal digest to observably different page reads.
  operands.domain = "TAFFY_DIRECT_OBSERVATION_DIGEST_V3";
  operands.scope = DirectObservationDocumentScope::kCurrentDocument;
  operands.include_child_frames = false;
  operands.max_nodes = core_service::mojom::kMaxDirectObservationNodes;
  operands.max_text_bytes = core_service::mojom::kMaxDirectObservationTextBytes;
  operands.max_total_bytes =
      core_service::mojom::kMaxDirectObservationTotalBytes;
  operands.max_frames = core_service::mojom::kMaxDirectObservationFrames;
  operands.deadline_ms = core_service::mojom::kMaxDirectObservationDeadlineMs;
  return operands;
}

std::string ComputeDirectObservationDigest(
    const DirectObservationContext& context,
    std::string_view profile_id,
    std::string_view direct_intent_id,
    std::string_view operation_id,
    std::string_view idempotency_key,
    uint64_t generation,
    const DirectObservationDigestOperands& operands) {
  std::string canonical;
  AppendDigestField(operands.domain, &canonical);
  AppendDigestField("DIRECT_USER_OBSERVATION", &canonical);
  AppendDigestField("DIRECT_USER_INTENT", &canonical);
  AppendDigestField("USER_ACTOR_LEASE", &canonical);
  AppendDigestField("ASSISTANT", &canonical);
  AppendDigestField("OBSERVE_PAGE", &canonical);
  AppendDigestField("LOCAL_READ", &canonical);
  AppendDigestField("NOT_SENSITIVE", &canonical);
  AppendDigestField(profile_id, &canonical);
  AppendDigestField(context.tab_id, &canonical);
  AppendDigestField(context.frame_id, &canonical);
  AppendDigestField(context.page_epoch, &canonical);
  AppendDigestField(context.origin, &canonical);
  AppendDigestField(direct_intent_id, &canonical);
  AppendDigestField(operation_id, &canonical);
  AppendDigestField(idempotency_key, &canonical);
  AppendDigestField(base::NumberToString(generation), &canonical);
  AppendDigestField(base::NumberToString(static_cast<uint8_t>(operands.scope)),
                    &canonical);
  AppendDigestField(operands.include_child_frames ? "true" : "false",
                    &canonical);
  AppendDigestField(base::NumberToString(operands.max_nodes), &canonical);
  AppendDigestField(base::NumberToString(operands.max_text_bytes), &canonical);
  AppendDigestField(base::NumberToString(operands.max_total_bytes), &canonical);
  AppendDigestField(base::NumberToString(operands.max_frames), &canonical);
  AppendDigestField(base::NumberToString(operands.deadline_ms), &canonical);
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(canonical)));
}

}  // namespace taffy
