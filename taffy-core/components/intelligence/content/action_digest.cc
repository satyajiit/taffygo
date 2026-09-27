// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_digest.h"

#include <string>

#include "base/numerics/byte_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
// VERIFY AT SP-01: upstream has been moving the SHA-256 helpers from
// crypto/sha2.h to crypto/hash.h. Confirm the spelling at the pinned
// milestone; only the hashing line in ComputeDigest changes.
#include "crypto/sha2.h"

namespace taffy {

namespace {

void AppendU64(std::string* out, uint64_t value) {
  const auto bytes = base::U64ToBigEndian(value);
  out->append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Length-prefixed. Without the prefix, moving a character between two adjacent
// strings would leave the digest unchanged.
void AppendString(std::string* out, const std::string& value) {
  AppendU64(out, value.size());
  out->append(value);
}

void AppendOrigin(std::string* out, const Origin& origin) {
  AppendU64(out, static_cast<uint64_t>(origin.kind));
  AppendString(out, origin.serialization);
  AppendString(out, origin.opaque_id);
}

void AppendNodeHandle(std::string* out,
                      const std::optional<NodeHandle>& handle) {
  AppendU64(out, handle.has_value() ? 1u : 0u);
  if (!handle) {
    return;
  }
  AppendString(out, handle->tab_id.value);
  AppendString(out, handle->frame_id.value);
  AppendString(out, handle->page_epoch.value);
  AppendU64(out, handle->graph_revision);
  AppendString(out, handle->node_id.value);
  AppendOrigin(out, handle->expected_origin);
}

void AppendDestination(std::string* out,
                       const std::optional<Destination>& destination) {
  AppendU64(out, destination.has_value() ? 1u : 0u);
  if (!destination) {
    return;
  }
  AppendOrigin(out, destination->url_metadata.origin);
  AppendU64(out, static_cast<uint64_t>(destination->url_metadata.disclosure));
  AppendString(out, destination->url_metadata.path.value_or(std::string()));
  AppendString(out, destination->url_metadata.url.value_or(std::string()));
  AppendU64(out, destination->url_metadata.has_query ? 1u : 0u);
  AppendU64(out, destination->url_metadata.has_fragment ? 1u : 0u);
  AppendU64(out, destination->is_cross_origin ? 1u : 0u);
  AppendU64(out, destination->opens_new_tab ? 1u : 0u);
  AppendU64(out, destination->is_download ? 1u : 0u);
}

void AppendPostconditions(std::string* out,
                          const std::vector<Postcondition>& postconditions) {
  AppendU64(out, postconditions.size());
  for (const Postcondition& postcondition : postconditions) {
    AppendU64(out, static_cast<uint64_t>(postcondition.kind));
    AppendDestination(out, postcondition.expected_destination);
    AppendU64(out, postcondition.allowed_origins.size());
    for (const Origin& origin : postcondition.allowed_origins) {
      AppendOrigin(out, origin);
    }
    AppendU64(out, postcondition.expected_node_state.has_value()
                       ? static_cast<uint64_t>(*postcondition.expected_node_state)
                       : 0xffffffffu);
    AppendU64(out, postcondition.timeout_ms);
  }
}

ContentDigest ComputeDigest(const std::string& canonical_encoding) {
  ContentDigest digest;
  digest.algorithm = DigestAlgorithm::kSha256;
  digest.value = base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(canonical_encoding)));
  return digest;
}

}  // namespace

ContentDigest ComputeActionDigest(
    const AuthorizedActionEnvelope& envelope) {
  // A canonical, length-prefixed encoding of everything that defines what the
  // action does.
  //
  // The capability's own fields are excluded on purpose. The digest answers
  // "is this the action that was authorized"; including the capability would
  // make it answer "is this the capability that was issued", which the ledger
  // already answers by identity.
  std::string buffer;
  AppendString(&buffer, "action");
  AppendU64(&buffer, static_cast<uint64_t>(envelope.action_type));
  AppendString(&buffer, envelope.task_id.value);
  AppendString(&buffer, envelope.action_id.value);
  AppendString(&buffer, envelope.target_handle.tab_id.value);
  AppendString(&buffer, envelope.target_handle.frame_id.value);
  AppendString(&buffer, envelope.target_handle.page_epoch.value);
  AppendU64(&buffer, envelope.target_handle.graph_revision);
  AppendString(&buffer, envelope.target_handle.node_id.value);
  AppendOrigin(&buffer, envelope.target_handle.expected_origin);
  AppendU64(&buffer, envelope.required_graph_revision);
  AppendU64(&buffer, static_cast<uint64_t>(envelope.idempotency_policy));
  AppendU64(&buffer, static_cast<uint64_t>(envelope.principal.kind));
  AppendString(&buffer, envelope.principal.skill_version_id
                            ? envelope.principal.skill_version_id->value
                            : std::string());

  // The input, and the one place in this encoding where what is *left out*
  // is the point.
  //
  // The value reference is hashed: swapping which held value a field is to
  // receive changes the action, so it has to change the digest the capability
  // was bound to. The reference is an opaque name minted from the browser's
  // own randomness and is not a function of the bytes, so hashing it discloses
  // nothing about them (bip_identity.h).
  //
  // The resolved value is not hashed, and cannot be: an envelope may name a
  // value and may never carry one, so `text` and `option_value` are empty here
  // by the shape rule the dispatcher enforces before this runs. Even if that
  // were relaxed, a digest of a form-field value would be the value. The
  // domains are tiny - a six-digit one-time code has a million members, a card
  // security code ten thousand, a date of birth about forty thousand - and a
  // digest over a domain that small is inverted by enumerating it, which a
  // phone does in well under a second. This digest reaches the policy engine,
  // the capability grant and the durable journal; a value's digest reaching
  // any of the three would be the value reaching all of them.
  AppendU64(&buffer, envelope.input.has_value() ? 1u : 0u);
  if (envelope.input) {
    AppendU64(&buffer, static_cast<uint64_t>(envelope.input->kind));
    AppendU64(&buffer, static_cast<uint64_t>(envelope.input->sensitivity));
    AppendString(&buffer, envelope.input->value_reference
                              ? envelope.input->value_reference->value
                              : std::string());
    AppendString(&buffer, envelope.input->text.value_or(std::string()));
    AppendString(&buffer, envelope.input->option_value.value_or(std::string()));
    AppendU64(&buffer, envelope.input->checked.value_or(false) ? 1u : 0u);
  }

  AppendU64(&buffer, envelope.preconditions.size());
  for (const Precondition& precondition : envelope.preconditions) {
    AppendU64(&buffer, static_cast<uint64_t>(precondition.kind));
    AppendString(&buffer, precondition.page_epoch
                              ? precondition.page_epoch->value
                              : std::string());
    AppendU64(&buffer, precondition.min_graph_revision.value_or(0));
    AppendOrigin(&buffer, precondition.origin.value_or(Origin()));
    AppendU64(&buffer, precondition.allowed_origins.size());
    for (const Origin& origin : precondition.allowed_origins) {
      AppendOrigin(&buffer, origin);
    }
    AppendU64(&buffer, precondition.expected_role.value_or(0xffff));
    AppendU64(&buffer,
              precondition.expected_action_type.has_value()
                  ? static_cast<uint64_t>(*precondition.expected_action_type)
                  : 0xffffffffu);
    AppendU64(&buffer, precondition.node_state.has_value()
                           ? static_cast<uint64_t>(*precondition.node_state)
                           : 0xffffffffu);
    AppendDestination(&buffer, precondition.expected_destination);
    AppendString(&buffer, precondition.expected_value_digest
                              ? precondition.expected_value_digest->value
                              : std::string());
    AppendU64(&buffer, precondition.max_sensitivity.has_value()
                           ? static_cast<uint64_t>(*precondition.max_sensitivity)
                           : 0xffffffffu);
    AppendU64(&buffer, precondition.min_content_trust.has_value()
                           ? static_cast<uint64_t>(
                                 *precondition.min_content_trust)
                           : 0xffffffffu);
  }

  AppendPostconditions(&buffer, envelope.expected_postconditions);
  AppendU64(&buffer, envelope.absolute_deadline_monotonic_ms);
  return ComputeDigest(buffer);
}

ContentDigest ComputeBrowserCommandDigest(
    const AuthorizedBrowserCommand& command) {
  std::string buffer;
  AppendString(&buffer, "browser_command");
  AppendU64(&buffer, static_cast<uint64_t>(command.command_type));
  AppendString(&buffer, command.task_id.value);
  AppendString(&buffer, command.action_id.value);
  AppendString(&buffer, command.tab_id.value);
  AppendNodeHandle(&buffer, command.source_handle);
  AppendString(&buffer, command.argument);
  AppendU64(&buffer, static_cast<uint64_t>(command.idempotency_policy));
  AppendU64(&buffer, static_cast<uint64_t>(command.principal.kind));
  AppendPostconditions(&buffer, command.expected_postconditions);
  AppendU64(&buffer, command.absolute_deadline_monotonic_ms);
  return ComputeDigest(buffer);
}

}  // namespace taffy
