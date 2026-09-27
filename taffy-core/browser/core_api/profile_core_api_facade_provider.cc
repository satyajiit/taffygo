// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The provider seam of the profile facade.
//
// Four properties hold across every handler here and none of them is
// optional.
//
// **A credential never comes back.** Every method on this seam answers with
// `CoreApiSubmissionStatus` alone — one closed enumeration, eight values, no
// field that could carry a string. So there is no response shape in which a
// key, a fragment of one, or its length could travel, and that is a property
// of the contract rather than of the code below.
//
// **Nothing is validated after state is touched.** The factory checks every
// field of the request before it mints an operation identity, so a refused
// request has spent no idempotency key and the core has not been told about
// it. The handler cannot reorder that: it has nothing to submit until the
// factory hands it a command.
//
// **The browser's own file is written before the core is told.** Six handlers
// write one — the standing model choice, the two that maintain the provider
// register of decision 0096, and the three that maintain the credential
// register of decision 0117 — and all six write after the build and before the
// forward. Before, because a core holding a fact this profile's
// preference file does not hold is a disagreement nothing corrects and nobody
// sees: the surface draws the core's answer while the file quietly says
// something else. After the build, because a file must never hold a row the
// factory would have refused.
//
// **The state change is published.** Not here — publication is the core's,
// through `response_after_change` in the service bridge — but that is the
// half this file depends on and the reason a bare `response(...)` there would
// make every one of these handlers silently useless. AGENTS.md records the
// delivery bridge doing exactly that: artifacts downloaded, verified and
// installed while every screen went on drawing the snapshot taken at
// bootstrap.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/browser/model/provider_credential_announcement_store.h"
#include "taffy/browser/model/provider_model_preference_store.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace

// A refused request and a submitted one leave by the same door, so a handler
// cannot accidentally answer twice or forget to answer at all.
void ProfileCoreApiFacade::SubmitProviderResult(
    ProviderCommandResult result, SubmissionCallback callback) {
  if (!result.command) {
    // The named reason is `result.refusal`. The contract's status enumeration
    // cannot carry it, and inventing a value would be a wire change; it is
    // asserted in core_api_command_factory_provider_refusal_unittest.cc
    // instead, one case per refusal, so the reason is under test even though
    // it is not transmitted.
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  SubmitProjected(std::move(result.command), std::move(callback));
}

// The save that announces a credential, and records that it did.
//
// The record is what makes the credential outlive this core generation. The
// core's provider plane holds a credential for exactly as long as the utility
// process it lives in, and nothing refills it, so a save that told the core
// and wrote nothing down works until the next start and is then a provider
// whose every request is unreachable while its row still reads connected
// (decision 0117). Written before the forward for the reason the register of
// decision 0096 is: this is the direction of disagreement that leaves a
// provider that cannot work.
void ProfileCoreApiFacade::SaveProviderCredential(
    const std::string &provider_id,
    core_api::mojom::ProviderAuthMethodView auth_method,
    const std::string &credential_handle,
    SaveProviderCredentialCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildSaveProviderCredential(
      provider_id, auth_method, credential_handle,
      manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis());
  // Built first, so the register never holds a row the factory would have
  // refused — a credential announced under an identity that cannot exist is a
  // command replayed into every future generation and refused by each one.
  if (!result.command) {
    SubmitProviderResult(std::move(result), std::move(callback));
    return;
  }
  if (!manager_ || !WriteProviderCredentialAnnouncement(
                       manager_->profile_prefs(), provider_id, auth_method,
                       credential_handle)) {
    std::move(callback).Run(manager_ ? SubmissionStatus::kInvalidRequest
                                     : SubmissionStatus::kCoreUnavailable);
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

// The forget that revokes a credential, and stops announcing it.
//
// The record goes before the command does, and a record this handler could not
// clear stops the forget: a register still naming a credential a person
// revoked would announce it again at the next start, which is the one outcome
// a revoke exists to prevent. The material itself is already gone by the time
// this runs — the Android store is revoked first, deliberately, so a key can
// always be removed from a person's own device.
void ProfileCoreApiFacade::ForgetProviderCredential(
    const std::string &provider_id, ForgetProviderCredentialCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildForgetProviderCredential(
      provider_id, manager_ ? manager_->service_generation() : 0u,
      NowMonotonicMillis());
  if (!result.command) {
    SubmitProviderResult(std::move(result), std::move(callback));
    return;
  }
  if (!manager_ || !ForgetProviderCredentialAnnouncement(
                       manager_->profile_prefs(), provider_id)) {
    std::move(callback).Run(manager_ ? SubmissionStatus::kInvalidRequest
                                     : SubmissionStatus::kCoreUnavailable);
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

// The state report, recorded so a restart does not undo it.
//
// A credential the browser knows needs a sign-in must not come back usable at
// the next start: the person would be answered with a request that fails
// rather than with the sign-in they can act on, and the failure would repeat
// for the life of the process.
//
// A report for a provider with no announced credential is refused rather than
// filed. A state is a fact about a credential, and there is none here to state
// it about — inventing a row would announce a handle nothing ever sealed.
void ProfileCoreApiFacade::SetProviderCredentialState(
    const std::string &provider_id,
    core_api::mojom::ProviderCredentialStateView state,
    SetProviderCredentialStateCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildSetProviderCredentialState(
      provider_id, state, manager_ ? manager_->service_generation() : 0u,
      NowMonotonicMillis());
  if (!result.command) {
    SubmitProviderResult(std::move(result), std::move(callback));
    return;
  }
  if (!manager_ || !WriteProviderCredentialAnnouncementState(
                       manager_->profile_prefs(), provider_id, state)) {
    std::move(callback).Run(manager_ ? SubmissionStatus::kInvalidRequest
                                     : SubmissionStatus::kCoreUnavailable);
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

void ProfileCoreApiFacade::ProbeProviderKey(
    const std::string &provider_id, const std::string &credential_handle,
    ProbeProviderKeyCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProviderResult(
      factory.BuildProbeProviderKey(
          provider_id, credential_handle,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

// The save that registers an address with the browser (decision 0096
// section 1).
//
// The register is what makes a person's own endpoint usable at all: a model
// request naming one is accepted only when this browser holds that exact
// string for that provider, so a save that told the core about an address and
// wrote nothing down would be a save whose every later request is refused as
// denied, with nothing a person could read. Written before the forward for
// that reason — the direction of the disagreement matters, and this is the one
// direction that leaves a provider that cannot work.
//
// **The string registered is read out of the built command, not off the
// argument.** They are the same bytes today, because the builder copies the
// endpoint and changes nothing about it. Reading it back out of the command is
// what keeps them the same bytes tomorrow: the comparison at send time is byte
// equality against this row, so any normalization a builder ever applied would
// have to be applied identically here, and taking the value from the one place
// that is definitely being sent removes the chance to get that wrong. There is
// no normalization here to keep in step, and that is the design rather than an
// omission — the register's whole value is that it holds what somebody typed.
void ProfileCoreApiFacade::SaveCustomProvider(
    const std::string &provider_id, const std::string &display_name,
    const std::string &endpoint, core_api::mojom::ProviderWireApiView wire_api,
    const std::optional<std::string> &credential_handle,
    std::vector<core_api::mojom::CustomModelSpecViewPtr> models,
    core_api::mojom::DetectedServerViewPtr detected_server,
    SaveCustomProviderCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  // The roster and the runtime the surface probed, forwarded exactly as it
  // stated them (decision 0096 section 4). Both are whole-state: an empty
  // roster is a provider with no declared models rather than a save that
  // leaves a previous roster standing, and an absent runtime is a surface
  // saying it recognised none rather than one it declined to mention.
  //
  // Core API 3.18 is where the method caught up with the body it carries. The
  // save's record has carried both since 3.13 and the call that fills it took
  // neither, so decision 0096 section 4 was unimplementable for five minors
  // while every gate passed — the browser could only ever pass an empty roster,
  // and a custom provider therefore always arrived with nothing to route to.
  //
  // The record is unwrapped to the one field it holds because the builder takes
  // the kind rather than the record; there is nothing else on it to lose.
  ProviderCommandResult result = factory.BuildSaveCustomProvider(
      provider_id, display_name, endpoint, wire_api, credential_handle,
      std::move(models),
      detected_server ? std::optional<core_api::mojom::ServerKindView>(
                            detected_server->server_kind)
                      : std::optional<core_api::mojom::ServerKindView>(),
      manager_->service_generation(), NowMonotonicMillis());
  if (!result.command) {
    // Refused by the factory, and `result.refusal` names which field. Nothing
    // is registered for a request that was never a command.
    SubmitProviderResult(std::move(result), std::move(callback));
    return;
  }
  const core_service::mojom::SaveCustomProviderCommand *saved =
      result.command->core_service_command
          ? result.command->core_service_command->save_custom_provider.get()
          : nullptr;
  if (!saved) {
    // A built command with no body to read the endpoint out of. Unreachable
    // from the builder above and refused rather than forwarded, because a save
    // that registered nothing is precisely the state this handler exists to
    // prevent.
    SubmitProviderResult(ProviderCommandResult::Refused(
                             ProviderRequestRefusal::kMalformedCommand),
                         std::move(callback));
    return;
  }
  // The register's own refusal, which is its bound: it admits a correction to
  // an address it already holds and refuses a *new* provider past
  // `MAX_CUSTOM_PROVIDERS`, the same number the core's provider store stops
  // at. Every other reason it could refuse — an identity that is not one, an
  // address the policy will not hold — the factory has already refused above,
  // so the bound is what is left and the refusal is named as it.
  //
  // Named through `ProviderCommandResult` rather than answered here, so this
  // reason travels the way every other provider refusal does and a surface has
  // one vocabulary to read. The command is dropped with it: a core told about
  // a provider whose address this browser did not write down would hold a
  // provider every later request refuses as denied.
  //
  // The whole definition and not the address alone, because the address alone
  // is not enough to tell the next core generation this provider exists
  // (decision 0117). The name, the wire family, the models and the runtime the
  // probe named are all non-secret and all stated by this one save; the key
  // stays in the Android store and only its handle is written here.
  CustomProviderDefinition definition;
  definition.provider_id = saved->provider_id;
  definition.display_name = saved->display_name;
  definition.endpoint = saved->endpoint;
  definition.wire_api = wire_api;
  definition.credential_handle = credential_handle;
  definition.detected_server =
      detected_server ? std::optional<core_api::mojom::ServerKindView>(
                            detected_server->server_kind)
                      : std::optional<core_api::mojom::ServerKindView>();
  for (const core_service::mojom::CustomModelSpecPtr &model : saved->models) {
    if (!model) {
      continue;
    }
    definition.models.push_back(CustomProviderModel{
        model->model_id, model->display_name, model->context_window,
        model->max_output_tokens, model->reasoning, model->tool_calling});
  }
  if (!WriteCustomProviderDefinition(manager_->profile_prefs(), definition)) {
    SubmitProviderResult(
        ProviderCommandResult::Refused(
            ProviderRequestRefusal::kTooManyCustomProviders),
        std::move(callback));
    return;
  }
  // The credential this save carries is announced the way every other one is,
  // so a state reported about it later has a row to land on and a replay
  // states it once more after the provider exists. A save carrying no
  // credential clears any announcement, because this write is whole-state: the
  // plane drops the credential on exactly the same command.
  const bool announced =
      credential_handle
          ? WriteProviderCredentialAnnouncement(
                manager_->profile_prefs(), saved->provider_id,
                core_api::mojom::ProviderAuthMethodView::kApiKey,
                *credential_handle)
          : ForgetProviderCredentialAnnouncement(manager_->profile_prefs(),
                                                 saved->provider_id);
  if (!announced) {
    SubmitProviderResult(ProviderCommandResult::Refused(
                             ProviderRequestRefusal::kMalformedCommand),
                         std::move(callback));
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

// The standing model choice, which writes before it forwards for the reason
// the file header gives.
//
// The browser's preference file is the authority for a standing choice, and
// the core is told at every generation (decision 0093, and decision 0080's
// rule for the served catalog). So a write that did not happen must not be
// forwarded: a core holding a choice this store could not replay would
// disagree with the file at the next start, and the disagreement would be
// invisible — the surface would draw the core's answer and the file would
// quietly say something else. `WriteProviderModelPreference` answering false
// is therefore refused here rather than reported as accepted.
void ProfileCoreApiFacade::SetProviderModelPreference(
    const std::string &provider_id, const std::optional<std::string> &model_id,
    core_api::mojom::ThinkingPreferenceViewPtr thinking,
    SetProviderModelPreferenceCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<core_api::mojom::ThinkingLevelView> level =
      thinking ? std::optional<core_api::mojom::ThinkingLevelView>(
                     thinking->level)
               : std::nullopt;
  // Built before it is written, and written before it is forwarded.
  //
  // The build is first so the store never files a row the factory would have
  // refused — a preference about a provider identity the roster cannot hold is
  // a preference about a provider that cannot exist, and a file holding one
  // would replay it into every future generation.
  //
  // The write is still before the forward, which is the half that matters: a
  // core told a choice the file does not hold would disagree with the file at
  // the next start, and the disagreement is invisible — the surface draws the
  // core's answer while the file quietly says something else.
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildSetProviderModelPreference(
      provider_id, model_id, level, manager_->service_generation(),
      NowMonotonicMillis());
  if (!result.command ||
      !WriteProviderModelPreference(manager_->profile_prefs(), provider_id,
                                    model_id, level)) {
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

// The endpoint question, asked before a provider exists to ask it about.
//
// **It writes nothing, and that is the whole difference between it and the
// save above.** A probe is a request to spend one bounded call proving that an
// address answers, and nothing durable changes on this command alone — there
// is no provider yet, so there is nothing for a register row to belong to, and
// a row written here would be an address the core could name for a provider
// nobody created. The address rule is the save's, though, because an address a
// probe may reach is an address this product is about to fetch from; that half
// is applied in the factory, at the one place both commands pass through.
//
// `provider_id` is the draft identity the verdict is filed under and does not
// change that: it is forwarded and never written down here. It comes from the
// surface rather than from this process because the save reuses it, so
// probing an address and then saving it produces one roster row instead of
// two, and a verdict that arrived while the person was still typing is already
// filed under the row the save will create.
void ProfileCoreApiFacade::ProbeCustomEndpoint(
    const std::string &endpoint, core_api::mojom::ProviderWireApiView wire_api,
    const std::optional<std::string> &credential_handle,
    const std::string &provider_id, ProbeCustomEndpointCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProviderResult(
      factory.BuildProbeCustomEndpoint(
          provider_id, endpoint, wire_api, credential_handle,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

// Removing the provider withdraws the address with it.
//
// A registered address that outlived the provider it belongs to is an address
// the core could still name, and the register would still recognise: the one
// authority that decides where a person's own model request may go would go on
// saying yes to a server nobody is configured to use. So the row is forgotten
// here, and it is forgotten *before* the removal is forwarded, which is the
// fail-closed order of the two. If the core then refuses the removal, the
// provider survives with no registered address and every request to it is
// denied — visible, and correctable by removing it again. The other order
// leaves the address standing when the removal is the half that fails, which
// is the case this file exists to prevent.
void ProfileCoreApiFacade::RemoveCustomProvider(
    const std::string &provider_id, RemoveCustomProviderCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildRemoveCustomProvider(
      provider_id, manager_->service_generation(), NowMonotonicMillis());
  // Built first, so a provider identity the factory would refuse never reaches
  // the register: forgetting a row under a name that could not have been saved
  // is a write with nothing to remove, and it would make a malformed request
  // look like it did something. The refusal leaves by the same door as every
  // other, carrying the name of the field that was wrong.
  if (!result.command) {
    SubmitProviderResult(std::move(result), std::move(callback));
    return;
  }
  // Forgetting has no bound and no address it will refuse to drop — it
  // succeeds for a provider that never had one, because the state afterwards
  // is the state that was asked for. So the only way this fails is a profile
  // with no preference file at all, which is not a state this browser runs in;
  // it is refused rather than forwarded, because a removal that left the
  // address standing is the one outcome this handler exists to prevent.
  if (!ForgetCustomProviderEndpoint(manager_->profile_prefs(), provider_id) ||
      !ForgetProviderCredentialAnnouncement(manager_->profile_prefs(),
                                            provider_id)) {
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }
  SubmitProviderResult(std::move(result), std::move(callback));
}

} // namespace taffy
