// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_H_
#define TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/model/custom_endpoint_prober.h"
#include "taffy/browser/model/profile_model_broker_route.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
namespace mojom {
class URLResponseHead;
}  // namespace mojom
}  // namespace network

namespace taffy {

class ProfilePageMediaStore;

// The browser-process half of one profile's model calls.
//
// The core service is sandboxed and has no socket, so `Effect::CallModel`
// leaves it as a typed effect, this class performs the request, and one
// typed completion returns. It is an HTTPS request in every case but one: an
// address a person typed and this browser wrote down may be plain http to a
// literal local machine, which is decision 0096 section 3 and the only reason
// cleartext is spoken here at all. Decision
// `docs/decisions/0052-the-assistant-loop-is-a-reducer.md` section 2 is the
// record, and the three things it says the core never learns are the three
// things this class owns: the credential, the route under the provider's
// origin, and whether the transport did anything at all beyond sending once.
//
// **The body is the core's; the route and the credential are not.** What is
// posted was written by `model-router`'s wire writers inside the sandbox. What
// this class adds is exactly four things — the path from the table in
// `profile_model_broker_routes.cc`, the credential from the profile's secure
// store, the one host substitution that credential may name (see
// `ResolveCredentialOrigin`, and it is the credential's word rather than
// the core's), and the transport settings a request needs to be safe. It adds
// nothing else, and in particular it never repairs an effect: an effect that
// does not pass `IsValidCoreModelRequest` is refused whole, because a repaired
// one would be a request nobody proposed, sent on a person's credential.
//
// **Nothing a call carries is logged.** Not the credential, not the request
// body, not the reply, not the address. The subsystem has exactly one `LOG`
// statement — `[taffy_model_transport_error]` in
// profile_model_broker_outcome.cc, which writes the Chromium error name and
// the HTTP status of a failed call and nothing else (decision 0217) — and no
// `DLOG` or `VLOG`. That is the guarantee rather than a habit: prompt and
// completion text is page content by the time it reaches a provider, and a
// credential written once to a log is a credential in a bug report.
//
// **Reply bytes are handed on unread.** Parsing a provider's JSON is parsing
// untrusted structure, and this is the browser process, which holds every
// capability (Rule of Two). Task replies cross in bounded chunks with network
// delivery paused until the isolated decoder consumes each one; task-less
// protocols retain one bounded `completion`. `model-router` is the only party
// that reads either shape, because usage and stop reason sit in a different
// place in each family. `ModelEffectResult::input_units` and `output_units`
// therefore leave here at zero.
class ProfileModelBroker final {
 public:
  using EffectCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;

  // Phase two of credential resolution: the answer.
  //
  // `std::nullopt` is a refusal — nothing configured, a vault that would not
  // open, a resolver that went away. It is deliberately not an empty string,
  // because an empty credential is a request that would reach the provider
  // and be refused there, spending an attempt to learn something this process
  // already knew.
  using CredentialCallback =
      base::OnceCallback<void(std::optional<std::string>)>;

  // Phase one: the question.
  //
  // Two phases rather than one because the far end of this seam is Kotlin's
  // `TaffyProfilePlatformAdapter.ResolveProviderCredential`, which opens the
  // sealed record through the same `suspend fun` the store already owns. That
  // work cannot answer inside a blocking JNI call without stalling the UI
  // thread that is making it, which is why the platform adapter is a Mojo
  // method and not a fifth native entry. The browser asks, returns, and is
  // resumed. Anything that assumed an answer was available synchronously
  // would either deadlock the thread the browser draws on or, worse, be
  // "fixed" by moving the secret somewhere it could be read without asking.
  //
  // The resolver must run its callback exactly once. A resolver that drops it
  // is handled — the call is refused rather than left pending forever — but
  // that is a safety net, not the contract.
  //
  // Its answer carries a second value the transient spender's does not: the
  // origin the sealed record says this particular credential's requests
  // belong to. One vendor issues that address together with the token, so
  // which host applies is a fact about the credential rather than about the
  // catalog, and the sandboxed core can therefore never state it — it names
  // the catalog origin, as it always has, and the substitution below is this
  // process's. `std::nullopt` is the ordinary answer and means the address is
  // the one the catalog named.
  using ProviderCredentialCallback =
      base::OnceCallback<void(std::optional<std::string> material,
                              std::optional<std::string> credential_origin)>;
  using CredentialResolver =
      base::RepeatingCallback<void(const std::string& provider_id,
                                   const std::string& credential_handle,
                                   ProviderCredentialCallback)>;

  // Phase one of a managed call: one single-use entitlement token (decision
  // 0082). The provider is the profile's entitlement cache; `evict` says the
  // held state bought a refusal and a fresh mint is wanted. nullopt is a
  // refusal, mapped to unavailable — the worker was not reachable or no
  // usable session exists, neither of which is the person's configuration.
  using EntitlementTokenCallback =
      base::OnceCallback<void(std::optional<std::string>)>;
  using EntitlementTokenProvider =
      base::RepeatingCallback<void(bool evict, EntitlementTokenCallback)>;

  // The only route raw provider chunks may take into the sandbox. The
  // callback is deliberately part of the dispatcher: network delivery stays
  // paused until the isolated parser has consumed the bytes and the browser
  // has accepted any sanitized visible delta they produced.
  using ModelStreamChunkCallback =
      base::OnceCallback<void(core_service::mojom::ModelStreamChunkStatus)>;
  using ModelStreamChunkDispatcher =
      base::RepeatingCallback<void(core_service::mojom::ModelStreamChunkPtr,
                                   ModelStreamChunkCallback)>;

  // Phase one of a probe on a pasted draft (decision 0083): spend one
  // transient secure-store handle, exactly once. nullopt is "absent or
  // already spent" — the draft never became a request, so the probe answers
  // unavailable and the provider is honestly reported unreached.
  using TransientCredentialConsumer =
      base::RepeatingCallback<void(const std::string& credential_handle,
                                   CredentialCallback)>;

  // `managed_worker_origin` is the compiled managed-worker origin at the
  // production call site; empty or invalid refuses every managed effect,
  // fail-closed.
  ProfileModelBroker(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::string managed_worker_origin);
  ProfileModelBroker(const ProfileModelBroker&) = delete;
  ProfileModelBroker& operator=(const ProfileModelBroker&) = delete;
  ~ProfileModelBroker();

  // Installs the platform's answer to phase one. Until it is installed, an
  // effect naming a credential handle is refused as unavailable: the browser
  // cannot reach the secret store, which is a different thing from the person
  // having configured nothing.
  void SetCredentialResolver(CredentialResolver resolver);

  // Installs the managed route's token source. Until it is installed, a
  // managed effect is refused as unavailable for the same reason as above.
  void SetEntitlementTokenProvider(EntitlementTokenProvider provider);

  // Installs the poke this class runs when the worker refuses a managed call
  // for quota even on a freshly minted token — the signal the entitlement
  // refresh protocol rate-gates as QUOTA_REFUSED.
  void SetQuotaRefusedCallback(base::RepeatingClosure callback);

  void SetModelStreamChunkDispatcher(ModelStreamChunkDispatcher dispatcher);

  // Installs the profile's one-use custody for exact-node page pixels. A
  // request without this seam cannot spend a media handle and is refused
  // before any network request leaves.
  void SetPageMediaStore(scoped_refptr<ProfilePageMediaStore> store);

  // Installs the one-shot spender a probe on a pasted draft resolves through.
  // Until it is installed, such a probe is refused as unavailable: what is
  // missing is the browser's seam to its own transient store.
  void SetTransientCredentialConsumer(TransientCredentialConsumer consumer);

  // Installs the register of decision 0096: what this browser wrote down when
  // a person saved their own model server's address. Until it is installed,
  // an effect claiming one of those addresses is refused — the register is the
  // only authority that can accept such an address, and this class does not
  // get to decide one on its behalf.
  void SetRegisteredEndpointLookup(RegisteredEndpointLookup lookup);

  // Performs one model effect. `callback` runs exactly once.
  void Dispatch(core_service::mojom::EffectEnvelopePtr effect,
                EffectCallback callback);

  // Performs one endpoint probe (decision 0096 section 5). `callback` runs
  // exactly once.
  //
  // It lives here rather than in its own plane because it needs exactly what
  // this class already holds — the profile's loader factory, the one-shot
  // spender for a pasted draft's transient, and the store resolver for a
  // provider that already exists — and because it is the same kind of thing:
  // an outbound request to an address a person typed, with their credential on
  // it. What it is *not* is a model call, and it therefore composes no body,
  // spends no turn and reads no reply through the router's decoders. The three
  // questions it asks are in custom_endpoint_prober.cc.
  //
  // One at a time. The core already claims a single probe flight, and a second
  // arriving here is answered unreached rather than queued — a person watching
  // a spinner asked one question.
  //
  // **Never routed through `CoreEffectBroker`.** That broker journals a durable
  // intent before it dispatches, while this is a live question with no
  // durable owner to replay it. Its identity still names browser session,
  // generation and attempt so a late callback cannot correlate with another
  // incarnation. `CoreServiceManager::EmitEffect` answers it where it arrives,
  // as it does the listing fetch and the composer push (decisions 0099/0100).
  void DispatchEndpointProbe(core_service::mojom::EffectEnvelopePtr effect,
                             EffectCallback callback);

  // Stops every call this task owns. Bound to `CoreEffectBroker`'s
  // `cancel_task` handler, which claims the callbacks first and then asks
  // typed adapters to stop their exact work before it synthesizes anything.
  //
  // Whether the request had left this process decides the answer, and the
  // distinction is the whole reason this is not left to the broker's
  // fall-through: a call still waiting on phase one cost nothing and is
  // `CANCELLED`, while one already on the wire may have been billed and is
  // `OUTCOME_UNKNOWN`. A ledger that recorded the second as a cancellation
  // would be recording that no money was spent, which it does not know.
  void CancelTask(std::string_view task_id, uint64_t generation);

  // Drops every call from a generation that is gone. It runs no callback: the
  // effect broker owns generation teardown and synthesizes the terminal for
  // every effect it still holds, so answering here would be a second terminal
  // for the same effect. Destroying the loaders is what this exists to do.
  void CancelGeneration(uint64_t generation);

  size_t in_flight_count_for_testing() const;

 private:
  class ModelStreamConsumer final
      : public network::SimpleURLLoaderStreamConsumer {
   public:
    ModelStreamConsumer(base::WeakPtr<ProfileModelBroker> broker,
                        std::string effect_id);
    ModelStreamConsumer(const ModelStreamConsumer&) = delete;
    ModelStreamConsumer& operator=(const ModelStreamConsumer&) = delete;
    ~ModelStreamConsumer() override;

    void OnDataReceived(std::string_view data,
                        base::OnceClosure resume) override;
    void OnComplete(bool success) override;
    void OnRetry(base::OnceClosure start_retry) override;

   private:
    base::WeakPtr<ProfileModelBroker> broker_;
    const std::string effect_id_;
  };

  // The route records — `model_broker::ModelRoute` and the three header
  // shapes it is made of — are plain data in profile_model_broker_route.h.
  // They hold nothing of this class, which is why they are not nested here
  // the way the three records below are.

  // One call, from the moment it is accepted to the moment it is answered.
  struct PendingCall {
    PendingCall();
    PendingCall(const PendingCall&) = delete;
    PendingCall& operator=(const PendingCall&) = delete;
    ~PendingCall();

    core_service::mojom::EffectEnvelopePtr effect;
    EffectCallback callback;
    // Where this call goes, resolved before the credential is asked for. It is
    // held rather than recomputed so that the URL a request is sent to is
    // provably the one that was checked, and not a second derivation of it.
    GURL url;
    // The family's compiled-in facts, resolved once with the URL. Held for the
    // same reason: the header the credential is written into must be the one
    // the route was checked against, not the answer to a second lookup that
    // could be made with different arguments.
    model_broker::ModelRoute route;
    // Null until the request has left this process. Its presence is the fact
    // `CancelTask` reads to tell a free cancellation from a paid one.
    std::unique_ptr<network::SimpleURLLoader> loader;
    std::unique_ptr<ModelStreamConsumer> stream_consumer;
    std::string stream_piece;
    size_t stream_piece_offset = 0;
    base::OnceClosure stream_resume;
    uint64_t streamed_response_bytes = 0;
    uint32_t stream_sequence = 0;
    int response_http_status = 0;
    bool response_started = false;
    bool stream_abort_scheduled = false;
    // Whether this call rides the managed route on a minted token.
    bool managed = false;
    // Whether the one evict-and-remint retry a managed call gets has run.
    // One and exactly one: the first 401/402 can be a token that lapsed or
    // an entitlement that renewed since the mint, and a fresh token answers
    // both; a second is the worker's settled answer.
    bool managed_retry_used = false;
  };

  // The route table, keyed by the closed `ProviderWireApi` and the provider.
  // `std::nullopt` is a family this build cannot address, or a model id that
  // cannot be a path segment; both are refusals rather than repairs.
  //
  // The provider is a parameter because several vendors speak a family's exact
  // shape underneath a base path of their own — `/openai`, `/inference`,
  // `/api/paas`. The core still sends an origin and this process still owns
  // every path, which is the property that keeps a sandboxed core unable to
  // name where a request goes; what the provider selects is which compiled
  // prefix that path is built on.
  static std::optional<model_broker::ModelRoute> RouteFor(
      core_service::mojom::ProviderWireApi wire_api,
      std::string_view provider_id,
      std::string_view model_id);

  // The full request URL, or nothing. Takes an endpoint that has already
  // passed `IsValidCoreModelRequest`, and still checks that joining the
  // path left the origin alone.
  //
  // The kind decides which of the route's two paths is joined and how
  // (decision 0096 section 2). A catalog origin gets the whole compiled path
  // resolved against it and must be https, exactly as it always has. A
  // registered base URL keeps its port and its own base path, and the
  // operation is joined beneath that path rather than beside it — the address
  // is one this browser wrote down when a person typed it, and cleartext to a
  // literal local machine is the case decision 0096 exists to allow.
  static std::optional<GURL> ResolveUrl(
      core_service::mojom::ModelEndpointKind endpoint_kind,
      const std::string& endpoint,
      const model_broker::ModelRoute& route);

  // What a credential's own address means for one request.
  //
  // Three answers rather than two, and the third is why this is a type at all.
  // An optional said both "no substitution was called for" and "one was called
  // for and could not be made", and the only thing a caller could do with the
  // pair was send the request to the address the substitution existed to
  // replace. That was harmless for exactly as long as every catalog origin was
  // also an API origin, and it stopped being harmless the moment a row named a
  // licensing parent instead.
  enum class CredentialOriginVerdict {
    // Nothing to apply and nothing was expected. The URL resolved before the
    // credential was asked for is the address the request goes to.
    kUnchanged,
    // The credential named an address this process may use.
    kSubstituted,
    // Substitution was called for and produced no address. The call is
    // refused; nothing leaves.
    kRefused,
  };

  struct CredentialOriginOutcome {
    // Fail-closed, so that a value nobody filled in refuses the call rather
    // than sending it somewhere.
    CredentialOriginVerdict verdict = CredentialOriginVerdict::kRefused;
    // Carried only by kSubstituted, and left invalid otherwise, so that a
    // caller reading it without reading the verdict gets a URL nothing can be
    // sent to rather than the one the substitution was meant to replace.
    GURL url;
  };

  // The address one request is sent to, once the credential has had its say.
  //
  // Nothing else about the request changes: the path is the compiled one,
  // resolved by the same catalog arm `ResolveUrl` uses, so what a substitution
  // can move is the host and nothing under it.
  //
  // It is refused unless the vendor's row licenses the domain the credential
  // named and that domain is at or beneath the one the effect named. The
  // second of those is not a security bound, it is an honesty one: the
  // product's statement of where a model request went is composed from the
  // endpoint the core named, so an address that left that domain would make
  // the sentence a person is shown untrue.
  //
  // A refusal refuses the **call**. It never falls back to the catalog's own
  // address, because a row that licenses a credential-host domain names the
  // licensing parent of that domain as its origin and that parent is not an
  // API — see `ProviderCredentialNamesItsOwnHost`, which is also what tells a
  // credential that named no address (the ordinary answer, for every vendor
  // that issues none) from one whose address was expected and did not arrive.
  //
  // A person's own registered address is never substituted: the register
  // holds the string they typed (decision 0096 section 1), and replacing a
  // host inside it would be this process answering the question the register
  // exists to be the only answer to. What a credential's own address decides
  // for such a request is whether the credential is spent there at all
  // (decision 0116). Beneath the vendor's licensed suffix the request goes
  // unchanged; outside it, a credential that named its own host is refused
  // rather than sent to a host it was never issued for; and a credential that
  // named none is unaffected, because it never had an address to be spent
  // beneath.
  static CredentialOriginOutcome ResolveCredentialOrigin(
      const core_service::mojom::ModelRequestEffect& request,
      const std::optional<std::string>& credential_origin,
      const model_broker::ModelRoute& route);

  // Whether the core's static headers are ones this transport will carry. It
  // is a narrower question than the contract's: the contract refuses names a
  // credential travels in, and this refuses names *this class composes*, which
  // is a fact about this transport rather than about the protocol.
  static bool StaticHeadersAreCarryable(
      const std::vector<core_service::mojom::ModelStaticHeaderPtr>& headers);

  // One endpoint probe, from the moment it is accepted to the moment it is
  // answered. There is at most one, because the core claims a single flight
  // and the prober holds a single loader.
  struct PendingEndpointProbe {
    PendingEndpointProbe();
    PendingEndpointProbe(const PendingEndpointProbe&) = delete;
    PendingEndpointProbe& operator=(const PendingEndpointProbe&) = delete;
    ~PendingEndpointProbe();

    core_service::mojom::EffectEnvelopePtr effect;
    EffectCallback callback;
  };

  void OnCredentialResolved(std::string effect_id,
                            std::optional<std::string> credential,
                            std::optional<std::string> credential_origin);
  void OnEndpointProbeCredential(std::string effect_id,
                                 std::optional<std::string> credential);
  // The store resolver's two-value answer, whose second value a probe drops.
  void OnEndpointProbeResolved(std::string effect_id,
                               std::optional<std::string> credential,
                               std::optional<std::string> credential_origin);
  void OnEndpointProbeAnswered(std::string effect_id,
                               CustomEndpointProber::Answer answer);
  // Answers the held probe and forgets it. `answer` is null for every outcome
  // in which no address was asked, and the body still carries `reached=false`
  // rather than being left out — the core reads the flag, and an absent body
  // is a protocol error rather than a verdict.
  void FinishEndpointProbe(const std::string& effect_id,
                           core_service::mojom::EffectStatus status,
                           const CustomEndpointProber::Answer* answer);
  core_service::mojom::EffectResultPtr MakeEndpointProbeResult(
      const core_service::mojom::EffectEnvelope& effect,
      core_service::mojom::EffectStatus status,
      const CustomEndpointProber::Answer* answer) const;
  void OnEntitlementToken(std::string effect_id,
                          std::optional<std::string> token);
  void OnTransientCredential(std::string effect_id,
                             std::optional<std::string> credential);
  void StartRequest(const std::string& effect_id,
                    std::optional<std::string> credential);
  void BeginRequestPreparation(const std::string& effect_id);
  void OnModelResponseStarted(
      std::string effect_id,
      const GURL& final_url,
      const network::mojom::URLResponseHead& response_head);
  void OnModelStreamData(std::string effect_id,
                         std::string_view data,
                         base::OnceClosure resume);
  void SendNextModelStreamChunk(const std::string& effect_id);
  void OnModelStreamChunkDelivered(
      std::string effect_id,
      core_service::mojom::ModelStreamChunkStatus status);
  void OnModelStreamComplete(std::string effect_id, bool success);
  void AbortModelStream(const std::string& effect_id,
                        core_service::mojom::EffectStatus status);
  void OnResponse(std::string effect_id,
                  std::optional<std::string> response_body);

  core_service::mojom::EffectResultPtr MakeResult(
      const core_service::mojom::EffectEnvelope& effect,
      core_service::mojom::EffectStatus status,
      std::vector<uint8_t> completion,
      uint32_t provider_http_status = 0,
      bool streamed = false,
      std::optional<core_service::mojom::ModelErrorClass> error_class =
          std::nullopt,
      std::optional<uint64_t> retry_after_millis = std::nullopt) const;

  // Answers one call and forgets it. The entry is erased before the callback
  // runs, so a caller that dispatches again from inside it sees a broker that
  // is no longer holding this effect id.
  void Finish(const std::string& effect_id,
              core_service::mojom::EffectResultPtr result);
  void Refuse(const core_service::mojom::EffectEnvelope& effect,
              EffectCallback callback,
              core_service::mojom::EffectStatus status);

  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  const std::string managed_worker_origin_;
  CredentialResolver credential_resolver_;
  EntitlementTokenProvider entitlement_token_provider_;
  base::RepeatingClosure quota_refused_callback_;
  ModelStreamChunkDispatcher model_stream_chunk_dispatcher_;
  scoped_refptr<ProfilePageMediaStore> page_media_store_;
  TransientCredentialConsumer transient_credential_consumer_;
  RegisteredEndpointLookup registered_endpoint_lookup_;
  base::flat_map<std::string, std::unique_ptr<PendingCall>> in_flight_;
  // Built on first use, because most profiles never probe an address at all.
  std::unique_ptr<CustomEndpointProber> endpoint_prober_;
  std::unique_ptr<PendingEndpointProbe> endpoint_probe_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileModelBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_H_
