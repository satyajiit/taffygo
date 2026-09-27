// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The provider seam of the command factory.
//
// Two things make this file different from its siblings. The first is that a
// credential travels through it: never as material, only as an opaque handle
// the surface's secure store minted, and there is no field on either contract
// that could carry a key (decision 0049). Nothing here reads, copies, hashes,
// logs or measures a credential — the handle is moved into the command and
// that is the whole of its handling.
//
// The second is the endpoint, and there are **two** endpoint rules in this
// file rather than one. Decision 0049 records that the contract can bound the
// string and nothing more, that `Endpoint::new` in the core requires https and
// a host and deliberately stops there, and that **the browser owns the refusal
// of a hostile endpoint**. This is the browser. Decision 0096 section 2 then
// splits that refusal in two, because the two kinds of endpoint are named by
// different authorities and the questions are not the same one:
//
//   * `CheckCatalogProviderEndpoint` is the older rule and it judges an
//     address a served document named: https, a publicly routable host, and
//     spelled exactly as an origin. That is right for a catalog endpoint and
//     it does not move.
//   * `CheckCustomProviderEndpoint` decides what may enter the register: an
//     address a person typed themselves, with the port and base path their own
//     server has, and plain http when — and only when — the machine is a
//     literal local one. It defers the whole of that decision to
//     `ClassifyCustomProviderEndpoint`, which is also what the register itself
//     asks before writing a row, so there is exactly one answer to "may this be
//     registered?" in the product.
//
// The two are deliberately not written in terms of each other, and they are
// two functions rather than one with a flag for exactly that reason: relaxing
// either cannot relax the one beside it, because there is no edit that touches
// both.
//
// The transport half of the same split stays with ProfileModelBroker, which is
// the only other place either kind of endpoint is used: a catalog origin gets
// the compiled route resolved against it, a registered base URL gets the
// operation joined beneath the path a person typed, and neither join is
// written in terms of the other there either.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/base/ip_address.h"
#include "net/base/url_util.h"
#include "taffy/browser/core_api/core_api_command_factory_provider_internal.h"
#include "taffy/browser/model/custom_provider_endpoint_policy.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

// Whether the identity is one the Android provider store can file a record
// under: `[a-z0-9][a-z0-9-]{0,63}`. The first byte may not be the separator, so
// an identity can never be confused with a prefix or sort ahead of every real
// one. The same rule is enforced again in the core's `ProviderId::new`; this
// copy exists so a person is told which field is wrong rather than being told
// the whole command was invalid.
bool IsStoreProviderId(const std::string &value) {
  const char first = value.front();
  if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
    return false;
  }
  for (char character : value) {
    const bool allowed = (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

// Whether every byte of the name is whitespace. A name that draws as nothing
// leaves a row a person cannot tell from the one above it.
bool IsBlank(const std::string &value) {
  for (char character : value) {
    if (character != ' ' && character != '\t' && character != '\n' &&
        character != '\r' && character != '\f' && character != '\v') {
      return false;
    }
  }
  return true;
}

// The host refusal decision 0049 assigns to the browser, for a **catalog**
// endpoint.
//
// A literal address is classified rather than pattern-matched, because the
// patterns are the part that gets it wrong: `https://0x7f.1/` and
// `https://2130706433/` are both 127.0.0.1 once canonicalized, and
// `https://[::ffff:169.254.169.254]/` is the link-local metadata address
// wearing IPv6. GURL has already canonicalized the host by the time this runs,
// and `IsPubliclyRoutable` covers loopback, link-local, unique-local, private
// and unspecified ranges in both families at once.
bool NamesAPublicHost(const GURL &url) {
  if (net::IsLocalhost(url)) {
    return false;
  }
  net::IPAddress address;
  if (!address.AssignFromIPLiteral(url.HostNoBrackets())) {
    // A name rather than a literal. What it resolves to is not knowable here
    // and is not this layer's refusal to make: the request has not been sent,
    // and a check now would be a different answer from the one at connect
    // time. The transport is where a resolved address is refused.
    return true;
  }
  return address.IsPubliclyRoutable();
}

// Whether a catalog endpoint is spelled exactly as an origin.
//
// Serializing the parsed origin and comparing it with what arrived is the same
// check `IsHttpsOrigin` makes in core_model_effect_validation.cc, and it is
// deliberately the same rather than a compatible one: that function decides
// whether a catalog model request ever leaves this process, and an endpoint
// this file admitted that it refuses is an endpoint that can be configured and
// never used. A path, a trailing slash, a query, a fragment, a
// `user:password@` prefix, an explicit `:443` and an upper-case host all
// survive parsing and none of them survives Serialize(), so each is refused
// without being named — and so is the next one nobody thought of.
//
// The rule cannot be shared as code: `IsHttpsOrigin` is file-local to a target
// that names browser objects, and this one is in the host-runnable projection
// target that deliberately names none. It is held together by a test instead —
// `ACatalogEndpointIsOneTheTransportSendsTo` runs every endpoint this
// function accepts through `IsValidCoreModelRequest`.
bool IsSpelledAsAnOrigin(const GURL &url, const std::string &value) {
  const url::Origin origin = url::Origin::Create(url);
  return !origin.opaque() && origin.Serialize() == value;
}

std::optional<service::ProviderAuthMethod>
ProjectAuthMethod(api::ProviderAuthMethodView method) {
  switch (method) {
  case api::ProviderAuthMethodView::kApiKey:
    return service::ProviderAuthMethod::kApiKey;
  case api::ProviderAuthMethodView::kOauth:
    return service::ProviderAuthMethod::kOauth;
  }
  return std::nullopt;
}

std::optional<service::ProviderCredentialState>
ProjectCredentialState(api::ProviderCredentialStateView state) {
  switch (state) {
  case api::ProviderCredentialStateView::kUsable:
    return service::ProviderCredentialState::kUsable;
  case api::ProviderCredentialStateView::kNeedsSignIn:
    return service::ProviderCredentialState::kNeedsSignIn;
  case api::ProviderCredentialStateView::kRefreshFailed:
    return service::ProviderCredentialState::kRefreshFailed;
  }
  return std::nullopt;
}

} // namespace

// The six rules the custom-provider builders share with this file. They are
// at namespace scope rather than in the anonymous one above for that reason
// alone; core_api_command_factory_provider_internal.h declares them and states
// why a second copy of any of them would be a defect.

ProviderRequestRefusal CheckProviderId(const std::string &value) {
  if (value.empty()) {
    return ProviderRequestRefusal::kEmptyProviderId;
  }
  if (value.size() > api::kMaxProviderIdBytes) {
    return ProviderRequestRefusal::kProviderIdTooLong;
  }
  if (!IsStoreProviderId(value)) {
    return ProviderRequestRefusal::kProviderIdAlphabet;
  }
  return ProviderRequestRefusal::kNone;
}

ProviderRequestRefusal CheckProviderCredentialHandle(const std::string &value) {
  if (value.empty()) {
    return ProviderRequestRefusal::kEmptyCredentialHandle;
  }
  if (value.size() > kMaxProviderCredentialHandleBytes) {
    return ProviderRequestRefusal::kCredentialHandleTooLong;
  }
  return ProviderRequestRefusal::kNone;
}

ProviderRequestRefusal CheckProviderDisplayName(const std::string &value) {
  if (IsBlank(value)) {
    return ProviderRequestRefusal::kEmptyDisplayName;
  }
  if (value.size() > api::kMaxProviderDisplayNameBytes) {
    return ProviderRequestRefusal::kDisplayNameTooLong;
  }
  return ProviderRequestRefusal::kNone;
}

ProviderRequestRefusal
CheckCatalogProviderEndpoint(const std::string &value) {
  if (value.size() > api::kMaxProviderEndpointBytes) {
    return ProviderRequestRefusal::kEndpointTooLong;
  }
  const GURL url(value);
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme)) {
    return ProviderRequestRefusal::kEndpointNotHttps;
  }
  if (url.host().empty()) {
    return ProviderRequestRefusal::kEndpointNoHost;
  }
  // Asked before the spelling, because where the endpoint points is the more
  // useful thing to be told. An address of https://127.0.0.1/v1 has two things
  // wrong with it and only one of them is worth a sentence.
  if (!NamesAPublicHost(url)) {
    return ProviderRequestRefusal::kEndpointNotPublic;
  }
  if (!IsSpelledAsAnOrigin(url, value)) {
    return ProviderRequestRefusal::kEndpointNotAnOrigin;
  }
  return ProviderRequestRefusal::kNone;
}

ProviderRequestRefusal CheckCustomProviderEndpoint(const std::string &value) {
  // Every part of the decision is `ClassifyCustomProviderEndpoint`'s, and
  // nothing is decided again here. That matters more than it looks: the
  // register applies the same classifier on the way in, so a save this
  // function accepts is a save the register can hold, and a save it refuses is
  // one the register would have refused anyway. A second opinion at this seam
  // — even a stricter one — would be a provider a person can be told to fix
  // and still cannot save.
  //
  // What this function does own is the sentence a person is shown, one per
  // classification, in the classifier's own order.
  switch (ClassifyCustomProviderEndpoint(value)) {
  case CustomEndpointRefusal::kNone:
    return ProviderRequestRefusal::kNone;
  case CustomEndpointRefusal::kTooLong:
    return ProviderRequestRefusal::kEndpointTooLong;
  case CustomEndpointRefusal::kNotAnAddress:
    return ProviderRequestRefusal::kEndpointNotAnAddress;
  case CustomEndpointRefusal::kCarriesCredentials:
    return ProviderRequestRefusal::kEndpointCarriesCredentials;
  case CustomEndpointRefusal::kCarriesQuery:
    return ProviderRequestRefusal::kEndpointCarriesQuery;
  case CustomEndpointRefusal::kCarriesFragment:
    return ProviderRequestRefusal::kEndpointCarriesFragment;
  case CustomEndpointRefusal::kCleartextNotLocal:
    return ProviderRequestRefusal::kEndpointCleartextNotLocal;
  }
  // A classification this file has not been taught. Refused as malformed
  // rather than admitted, because the one thing worse than an unnamed refusal
  // is an address that was never classified reaching the register.
  return ProviderRequestRefusal::kMalformedCommand;
}

std::optional<service::ProviderWireApi>
ProjectProviderWireApi(api::ProviderWireApiView wire_api) {
  switch (wire_api) {
  case api::ProviderWireApiView::kAnthropicMessages:
    return service::ProviderWireApi::kAnthropicMessages;
  case api::ProviderWireApiView::kOpenAiResponses:
    return service::ProviderWireApi::kOpenAiResponses;
  case api::ProviderWireApiView::kOpenAiCompletions:
    return service::ProviderWireApi::kOpenAiCompletions;
  case api::ProviderWireApiView::kGoogleGenerativeLanguage:
    return service::ProviderWireApi::kGoogleGenerativeLanguage;
  case api::ProviderWireApiView::kManaged:
  case api::ProviderWireApiView::kOpenAiCodexResponses:
  case api::ProviderWireApiView::kGoogleCloudCodeAssist:
    // Three reserved dialects, refused for the same reason and by name rather
    // than by omission. The managed wire is the product's own envelope, spoken
    // only to the compiled worker origin; the other two are vendors'
    // subscription endpoints reached with a subscription credential. A
    // person's own endpoint is none of them, so a provider naming one is
    // refused rather than saved as a provider nothing would route to. The
    // core's provider plane refuses the same set independently — see
    // `ReservedWireApi` in provider_commands.rs.
    return std::nullopt;
  }
  return std::nullopt;
}

ProviderCommandResult::ProviderCommandResult() = default;
ProviderCommandResult::ProviderCommandResult(ProviderCommandResult &&) =
    default;
ProviderCommandResult &
ProviderCommandResult::operator=(ProviderCommandResult &&) = default;
ProviderCommandResult::~ProviderCommandResult() = default;

ProviderCommandResult
ProviderCommandResult::Refused(ProviderRequestRefusal refusal) {
  ProviderCommandResult result;
  result.refusal = refusal;
  return result;
}

ProviderCommandResult ProviderCommandResult::Built(ProjectedCoreCommand built) {
  ProviderCommandResult result;
  result.command = std::move(built);
  return result;
}

ProviderCommandResult CoreApiCommandFactory::BuildSaveProviderCredential(
    std::string provider_id, api::ProviderAuthMethodView method,
    std::string credential_handle, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (const ProviderRequestRefusal refusal =
          CheckProviderCredentialHandle(credential_handle);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  const std::optional<service::ProviderAuthMethod> projected_method =
      ProjectAuthMethod(method);
  if (!projected_method) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSaveProviderCredential;
  core_command->save_provider_credential =
      api::SaveProviderCredentialBody::New(provider_id, method,
                                           credential_handle);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kSaveProviderCredential;
  service_command->save_provider_credential =
      service::SaveProviderCredentialCommand::New(
          std::move(provider_id), *projected_method,
          std::move(credential_handle));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

ProviderCommandResult CoreApiCommandFactory::BuildForgetProviderCredential(
    std::string provider_id, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kForgetProviderCredential;
  core_command->forget_provider_credential =
      api::ForgetProviderCredentialBody::New(provider_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kForgetProviderCredential;
  service_command->forget_provider_credential =
      service::ForgetProviderCredentialCommand::New(std::move(provider_id));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// The state report decision 0078 adds: sent only by the browser layer from
// inside the provider's critical section, after a refresh or probe learned
// something the registry should reflect. It carries a name and a closed enum,
// never material, and the core refuses it for a provider holding no record —
// absence is a deletion, and a reporter that lost that race must observe the
// deletion rather than resurrect the row.
ProviderCommandResult CoreApiCommandFactory::BuildSetProviderCredentialState(
    std::string provider_id, api::ProviderCredentialStateView state,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  const std::optional<service::ProviderCredentialState> projected_state =
      ProjectCredentialState(state);
  if (!projected_state) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSetProviderCredentialState;
  core_command->set_provider_credential_state =
      api::SetProviderCredentialStateBody::New(provider_id, state);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kSetProviderCredentialState;
  // The roster of model ids this report was learned against is empty here, and
  // that is the honest value rather than a placeholder: a credential-state
  // report is what a refresh or a probe concluded about the key, and neither
  // enumerates models. The listing that does is the provider-listing fetch,
  // which reaches the core as its own effect result.
  service_command->set_provider_credential_state =
      service::SetProviderCredentialStateCommand::New(
          std::move(provider_id), *projected_state,
          std::vector<std::string>());
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// The probe decision 0083 adds. The same two fields as a save and the same
// checks, because the command is a request to spend one bounded model call on
// proving what a save would record — the core composes the call, the model
// broker performs it, and nothing durable changes on this command alone.
ProviderCommandResult CoreApiCommandFactory::BuildProbeProviderKey(
    std::string provider_id, std::string credential_handle,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (const ProviderRequestRefusal refusal =
          CheckProviderCredentialHandle(credential_handle);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kProbeProviderKey;
  core_command->probe_provider_key =
      api::ProbeProviderKeyBody::New(provider_id, credential_handle);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kProbeProviderCredential;
  service_command->probe_provider_credential =
      service::ProbeProviderCredentialCommand::New(
          std::move(provider_id), std::move(credential_handle));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// The flow identity and the redirect binding are minted here, exactly as they
// are for an account sign-in: a surface that chose either could replay one
// person's redirect into another's flow.
ProviderCommandResult CoreApiCommandFactory::BuildStartProviderAuth(
    std::string provider_id, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kStartProviderAuth;
  core_command->start_provider_auth =
      api::StartProviderAuthBody::New(provider_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kStartProviderAuth;
  service_command->start_provider_auth =
      service::StartProviderAuthCommand::New(
          entropy_source_->NewOpaqueId("provider-flow"), std::move(provider_id),
          entropy_source_->NewOpaqueId("provider-binding"), now_monotonic_ms);
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

ProviderCommandResult CoreApiCommandFactory::BuildCancelProviderAuth(
    std::string flow_id, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (flow_id.empty() || flow_id.size() > api::kMaxIdentifierBytes ||
      flow_id.size() > service::kMaxIdentifierBytes) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kCancelProviderAuth;
  core_command->cancel_provider_auth =
      api::CancelProviderAuthBody::New(flow_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kCancelProviderAuth;
  service_command->cancel_provider_auth =
      service::CancelProviderAuthCommand::New(std::move(flow_id));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// BuildSaveCustomProvider and BuildProbeCustomEndpoint are in
// core_api_command_factory_provider_custom.cc: both carry a model roster and
// a detected server beside the fields this file's checks bound, and the two
// halves together were over the file cap.

ProviderCommandResult CoreApiCommandFactory::BuildRemoveCustomProvider(
    std::string provider_id, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kRemoveCustomProvider;
  core_command->remove_custom_provider =
      api::RemoveCustomProviderBody::New(provider_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kRemoveCustomProvider;
  service_command->remove_custom_provider =
      service::RemoveCustomProviderCommand::New(std::move(provider_id));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

} // namespace taffy
