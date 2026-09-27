// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_API_CORE_API_PROJECTED_COMMAND_H_
#define TAFFY_BROWSER_CORE_API_CORE_API_PROJECTED_COMMAND_H_

#include <stddef.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

// What the command factory produces and the terms it produces it under: the
// browser's entropy seam, the projected pair of commands, the loop bounds a
// start carries, and the named refusal of a provider request. Split from the
// factory's own declaration, which reads these and is the one thing that
// builds them.

namespace taffy {

// Browser-owned entropy seam. Android never chooses an operation identity,
// task identity, idempotency key, trace identity, or reducer seed.
class CoreApiEntropySource {
 public:
  virtual ~CoreApiEntropySource() = default;
  virtual std::string NewOpaqueId(std::string_view domain) = 0;
  virtual std::array<uint8_t, 32> NewTaskSeed() = 0;
};

std::unique_ptr<CoreApiEntropySource> CreateCoreApiEntropySource();

struct ProjectedCoreCommand {
  core_api::mojom::CoreCommandPtr core_api_command;
  core_service::mojom::CoreServiceCommandPtr core_service_command;
};

// Longest provider credential handle this seam accepts.
//
// 128, and deliberately not the contract's `MAX_AUTH_CREDENTIAL_HANDLE_BYTES`
// of 256. That constant bounds an *account* flow's handle. A provider handle
// is bounded by `CredentialHandle::new` in the isolated core, which stops at
// 128, and decision 0049 section 4 states the rule this follows: the bound is
// the narrower of the two sides, so that a person is told which field is wrong
// instead of the last hop reporting that the whole command was invalid.
//
// Borrowing the account bound here accepted a handle of 129..256 bytes, minted
// an operation identity and an idempotency key for it, and left the core to
// answer `INVALID_COMMAND` with no field named — which is exactly what the
// checks in this file exist to prevent.
//
// Neither contract declares a provider-handle bound to read instead, and
// adding one is a minor bump on both and an owner's call. So the number is
// written out here with its source named, the way the provider-id alphabet is,
// and `ACredentialHandleIsBoundedAndNeverEmpty` pins both sides of it.
inline constexpr size_t kMaxProviderCredentialHandleBytes = 128u;

// First bound on model turns for a DirectUserKey start.
//
// Product numbers belong in the metric registry. Until that owns this limit,
// the factory states it and the core decoder refuses a DirectUserKey command
// whose MaxModelRequests is not exactly this value. Keep in step with
// `DIRECT_USER_KEY_MAX_MODEL_REQUESTS` in core-runtime's start-task decoder.
inline constexpr uint64_t kDirectUserKeyMaxModelRequests = 32u;

// Managed page tasks have the same runaway-loop bound as DirectUserKey. The
// managed worker applies entitlement, quota and spend limits independently;
// this number never fabricates any of those account facts.
inline constexpr uint64_t kManagedServiceMaxModelRequests = 32u;

// A Web errand may cross several pages, so its exact generated shape carries
// a larger loop bound for either model route. Keep in step with
// `ERRAND_MAX_MODEL_REQUESTS` in core-runtime's start-task decoder, which
// refuses a start command whose budget is not exactly this value: the two
// numbers disagreeing reaches a phone as "Taffy could not read this request"
// on the consent sheet, with the reason only in a log (decision 0177).
inline constexpr uint64_t kWebErrandMaxModelRequests = 160u;

// How many times one logical model turn may be paid for again after a
// retryable refusal (decision 0218).
//
// This is the budget half of a ceiling the model router already carries:
// `SEMANTIC_RETRY_ATTEMPTS` admits three paid attempts per turn, so two
// retries is the number that makes the two agree rather than letting one
// silently dominate the other. It is spent only on a class the router calls
// retryable — a provider overload or a transport failure — never on a rejected
// credential, a quota, or a request that will stay wrong.
//
// It must be stated. `ProductionTaskFactory` fills an unstated budget with
// zero, deliberately, so a limit nobody reviewed cannot be generous by
// accident; the consequence is that a limit nobody *states* is zero, and a
// zero here means no provider failure is ever retried. Keep in step with
// `MAX_RETRIES_PER_STEP` in core-runtime's start-task decoder.
inline constexpr uint64_t kMaxRetriesPerStep = 2u;

// A start that may make no model request retries nothing, because there is no
// paid attempt to repeat. Stated rather than omitted: the decoder checks the
// budget list exactly, and "absent" and "zero" must not be the same command.
inline constexpr uint64_t kNoModelMaxRetriesPerStep = 0u;

// Why one provider request was refused.
//
// The Core API answers a command with `CoreApiSubmissionStatus`, which has one
// value for every refusal a caller can cause: `INVALID_REQUEST`. That is the
// right wire answer — none of these is a race, a deadline or backpressure —
// but it is a useless one to debug against, so the reason is named here and
// the suite asserts on it. A surface that must explain the refusal to a person
// reads it from the request it sent: exactly one of these is reachable per
// field, and every one names the field it is about.
enum class ProviderRequestRefusal {
  kNone,
  kEmptyProviderId,
  kProviderIdTooLong,
  // The identity used a byte the Android provider store cannot file a record
  // under. Its alphabet is `[a-z0-9][a-z0-9-]{0,63}`, and it is the narrower
  // of the two sides, so refusing here names the real reason rather than
  // letting the last hop report "could not be changed on this device"
  // (decision 0049 section 4).
  kProviderIdAlphabet,
  kEmptyCredentialHandle,
  kCredentialHandleTooLong,
  kEmptyDisplayName,
  kDisplayNameTooLong,
  kEndpointTooLong,

  // The four refusals below belong to the **catalog** endpoint rule
  // (`CheckCatalogProviderEndpoint`), which asks whether an address a served
  // document named may be reached. They are deliberately not the refusals of
  // the register rule beneath them, and decision 0096 section 2 is why: a
  // catalog endpoint is judged and a person's own endpoint is recognized, and
  // relaxing either question must not relax the other. A shared vocabulary
  // would be the first step towards a shared rule.
  kEndpointNotHttps,
  // A guard rather than a refusal a caller can provoke. No spelling reaches
  // it: a standard URL with an empty host does not canonicalize at all, so
  // GURL reports it invalid and kEndpointNotHttps answers first, and a
  // spelling that looks hostless is not — "https:///v1" is host "v1", because
  // a standard scheme's authority swallows every leading slash. It is kept
  // because it is the one thing between a change in that behaviour and an
  // endpoint with no host at all, and
  // core_api_command_factory_provider_endpoint_unittest.cc records both
  // spellings so a change is a failing test rather than a surprise.
  kEndpointNoHost,
  // The endpoint was not spelled as an origin: it carried a path, a trailing
  // slash, a query, a fragment, user information, an explicit `:443` or an
  // upper-case host.
  //
  // This is the browser's own rule read back at whoever named the endpoint,
  // rather than a second opinion about endpoints. `IsValidCoreModelRequest`
  // refuses any catalog model request whose endpoint is not exactly an origin,
  // because the compiled route table owns the path and a core that could name
  // one could name any route on a host a person had already trusted with a
  // credential. Accepting a looser spelling would store an endpoint that every
  // later catalog request refuses as denied, with nothing a person could read.
  kEndpointNotAnOrigin,
  // The endpoint named a host that is not publicly routable — loopback,
  // link-local, unique-local or private. Decision 0049 records that
  // `Endpoint::new` deliberately stops at the scheme and that **the browser
  // owns this refusal**; `https://169.254.169.254/…` is structurally valid to
  // every layer below this one.
  kEndpointNotPublic,

  // The five refusals below belong to the **register** rule
  // (`CheckCustomProviderEndpoint`), which asks the different question of
  // decision 0096 section 3: may a person register this address as their own
  // model server's? Each is one sentence to show somebody who is looking at
  // their own server's address and cannot see what is wrong with it, which is
  // the whole reason they are named separately rather than collapsed into one
  // "bad endpoint".
  //
  // They arrive in the order `ClassifyCustomProviderEndpoint` decides them,
  // and that order is its own: a person who typed a query onto their endpoint
  // has a different thing to fix than one who typed http, and being told about
  // the scheme first would send them to fix the wrong one.

  // Nothing parses out of it, it names no host, or its scheme is neither https
  // nor http. It is not `kEndpointNotHttps`: http is admitted here, under the
  // condition below, so saying "not https" would be telling a person to change
  // the one thing that is allowed.
  kEndpointNotAnAddress,
  // Plain http to something that is not a literal local address. The address
  // is fine, the scheme is fine, and the pair is not — which is why this is
  // the refusal that most needs its own sentence.
  kEndpointCleartextNotLocal,
  // A `user:password@` prefix. A credential reaches a provider one way, and it
  // is not by being typed into an address bar (decision 0049).
  kEndpointCarriesCredentials,
  kEndpointCarriesQuery,
  kEndpointCarriesFragment,
  // The register already holds `MAX_CUSTOM_PROVIDERS` addresses and this save
  // would have added another. Refused rather than evicting one: which address
  // a person loses is not a decision this process gets to make quietly, and an
  // endpoint dropped from the register is a provider that stops working with
  // nothing said. Correcting an address already registered is always allowed —
  // the bound is on new providers, not on edits.
  //
  // The only refusal here that no builder produces. The factory holds no file
  // and cannot count what is in one, so the bound is the register's and this
  // name is minted by `ProfileCoreApiFacade::SaveCustomProvider` when the
  // registration it attempted was refused — through the same
  // `ProviderCommandResult` every other refusal travels in, so a surface reads
  // one vocabulary rather than two.
  kTooManyCustomProviders,
  // A model identity, on a declared roster row or on a standing choice. It is
  // bounded by the contract's own `MAX_MODEL_ID_BYTES` rather than by the
  // generic identifier bound, and an empty one is refused rather than read as
  // "no model": absence is spelled by leaving the field out.
  kEmptyModelId,
  kModelIdTooLong,
  // A model's own display name, which is not the provider's. Named separately
  // so a person is told which of the two rows is wrong.
  kEmptyModelDisplayName,
  kModelDisplayNameTooLong,
  // The declared roster was longer than `MAX_CUSTOM_MODEL_ENTRIES`. Refused
  // whole rather than truncated: a saved provider offering the first thirty-two
  // of a person's models, silently, is worse than one they were told to
  // shorten.
  kTooManyModels,
  // The generated body did not survive its own validity check. Unreachable
  // from a well-formed request and kept because "the factory built something
  // the contract rejects" must not be reported as a caller's mistake.
  kMalformedCommand,
};

// One provider request, either projected or refused by name. Exactly one of
// the two is set.
struct ProviderCommandResult {
  ProviderCommandResult();
  ProviderCommandResult(ProviderCommandResult&&);
  ProviderCommandResult& operator=(ProviderCommandResult&&);
  ~ProviderCommandResult();

  static ProviderCommandResult Refused(ProviderRequestRefusal refusal);
  static ProviderCommandResult Built(ProjectedCoreCommand command);

  ProviderRequestRefusal refusal = ProviderRequestRefusal::kNone;
  std::optional<ProjectedCoreCommand> command;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_API_CORE_API_PROJECTED_COMMAND_H_
