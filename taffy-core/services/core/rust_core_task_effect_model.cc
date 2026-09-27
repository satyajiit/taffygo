// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One task model effect, projected. It moved out of the binding's own file
// for the reason the action effect did before it — it had become the largest
// of the projections there — and it is the one that grew: a composed plan may
// now name per-conversation headers, which arrive as two parallel lists this
// file pairs and refuses rather than repairs (decision 0111 section 3).

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_records.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

// One model call, with the plan the sandbox composed for it.
//
// Two things this refuses are worth naming, because each is a way an effect
// could reach the network as something nobody planned. The enumerations are
// decoded rather than cast, so a byte naming no member fails closed here
// instead of arriving at a switch that cannot match it. And a credential is
// carried only as an opaque secure-store handle, present or absent with
// nothing in between — the key itself never enters this process, and there is
// no field here it could sit in.
//
// The request's owning task is the binding's, written once from one field. It
// is what `CoreEffectBroker::CancelTask` matches a cancellation against, so a
// second field that could disagree with it would be a paid call stopped under
// one task while a person cancelled the other. The Rust side refuses a
// composed plan whose task is not the binding's for the same reason.
//
// The endpoint is checked for length alone, and it travels with the authority
// that named it. Which of two proofs the browser owes it follows from that
// authority and never from the address's shape: a catalog origin is judged as
// an https origin, and a person's own base URL is matched against the register
// the browser itself holds (decision 0096). Both proofs are
// `IsValidCoreModelRequest`'s rather than this file's, because each needs a URL
// parser and this file's DEPS admits the contract and nothing else.
std::optional<mojom::TaskModelEffectPtr> ModelEffect(
    const bridge::BridgeTaskEffect& input) {
  const auto disclosure =
      ClosedEnum(input.disclosure, mojom::DisclosureClass::kPageContent);
  // `kMaxValue` rather than the last member named by hand. A ceiling written
  // as a name compiles perfectly after a member is added past it and then
  // refuses every effect that names the new family, saying nothing — no
  // compiler, no host lane and no Chromium build can see it, because the
  // constant is not wrong in any way a type checker reads.
  const auto wire_api =
      ClosedEnum(input.wire_api, mojom::ProviderWireApi::kMaxValue);
  const auto endpoint_kind =
      ClosedEnum(input.endpoint_kind, mojom::ModelEndpointKind::kUserBaseUrl);
  if (!disclosure || !wire_api || !endpoint_kind ||
      !ValidIdentifier(input.call_id) || !ValidIdentifier(input.route_id) ||
      !ValidIdentifier(input.model_id) || !ValidIdentifier(input.task_id) ||
      input.provider_id.empty() ||
      input.provider_id.size() > mojom::kMaxProviderIdBytes ||
      input.endpoint.empty() ||
      input.endpoint.size() > mojom::kMaxProviderEndpointBytes ||
      input.request_body.empty() ||
      input.request_body.size() > mojom::kMaxEffectBytes ||
      input.max_output_bytes == 0u ||
      input.max_output_bytes > mojom::kMaxEffectBytes ||
      (input.not_before_monotonic_ms != 0u &&
       input.not_before_monotonic_ms >=
           input.operation.deadline_monotonic_ms) ||
      input.has_credential_handle != !input.credential_handle.empty() ||
      (input.has_credential_handle &&
       !ValidIdentifier(input.credential_handle)) ||
      input.has_media_attachment != !input.media_attachment_handle.empty() ||
      input.has_media_attachment != !input.media_attachment_mime_type.empty() ||
      (input.has_media_attachment &&
       (*disclosure != mojom::DisclosureClass::kPageContent ||
        input.media_attachment_handle.size() >
            mojom::kMaxMediaAttachmentHandleBytes ||
        input.media_attachment_mime_type != "image/png"))) {
    return std::nullopt;
  }
  auto request = mojom::ModelRequestEffect::New();
  request->route_id = std::string(input.route_id);
  request->model_id = std::string(input.model_id);
  request->disclosure = *disclosure;
  request->request_body.assign(input.request_body.begin(),
                               input.request_body.end());
  request->max_output_bytes = input.max_output_bytes;
  request->not_before_monotonic_ms = input.not_before_monotonic_ms;
  request->task_id = std::string(input.task_id);
  request->provider_id = std::string(input.provider_id);
  request->wire_api = *wire_api;
  request->endpoint = std::string(input.endpoint);
  // Carried, and decoded rather than cast, for the reason decision 0096 gives:
  // the field says which rule the browser is being asked to apply, and a rule
  // claimed by default is a rule nobody claimed. The claim belongs to whichever
  // layer of the merged catalog supplied the candidate, which the composer
  // stamped beside the address in one decision; a byte naming no member is
  // refused above rather than settled here, because the settlement available
  // to this file is the catalog rule and that is exactly the direction a
  // person's own address must not be sent in.
  request->endpoint_kind = *endpoint_kind;
  if (input.has_credential_handle) {
    request->credential_handle = std::string(input.credential_handle);
  }
  if (input.has_media_attachment) {
    request->media_attachment_handle =
        std::string(input.media_attachment_handle);
    request->media_attachment_mime_type =
        std::string(input.media_attachment_mime_type);
  }
  // Two parallel lists, paired here and refused rather than repaired when
  // they disagree: a record whose names and values are of different lengths
  // describes no request, and pairing what there is of it would send a header
  // under a name the plan did not give it. What each name and value may be is
  // decided downstream by the browser's own validator, which is the party that
  // knows which names a credential travels in and which this transport
  // composes for itself.
  if (input.static_header_names.size() != input.static_header_values.size()) {
    return std::nullopt;
  }
  request->static_headers.reserve(input.static_header_names.size());
  for (size_t index = 0; index < input.static_header_names.size(); ++index) {
    auto header = mojom::ModelStaticHeader::New();
    header->name = std::string(input.static_header_names[index]);
    header->value = std::string(input.static_header_values[index]);
    request->static_headers.push_back(std::move(header));
  }
  auto out = mojom::TaskModelEffect::New();
  out->call_id = std::string(input.call_id);
  out->request = std::move(request);
  return out;
}
}  // namespace taffy::core_service_internal
