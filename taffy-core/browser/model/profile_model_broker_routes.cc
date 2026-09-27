// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/model/profile_model_broker.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "taffy/browser/model/profile_model_broker_route.h"
#include "taffy/browser/providerauth/provider_credential_host.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

using model_broker::CredentialClaimHeader;
using model_broker::CredentialPlacement;
using model_broker::FamilyHeader;
using model_broker::ModelRoute;

// Whether a model id may be spelled into a path segment.
//
// The one family whose route names the model is the reason this exists, and it
// refuses rather than escapes. Escaping would accept `../v1/messages` and
// `a/b`, turn each into something harmless, and thereby accept as a model id a
// string that was never one — the defect surviving as a request that works.
// Refusing keeps the failure where the wrong value was chosen. Nothing
// legitimate is lost: every published model id in all four families is drawn
// from this alphabet.
bool IsPathSafeModelId(std::string_view model_id) {
  if (model_id.empty()) {
    return false;
  }
  for (char character : model_id) {
    if (!base::IsAsciiAlphaNumeric(character) && character != '-' &&
        character != '_' && character != '.') {
      return false;
    }
  }
  return true;
}

// One vendor's base path, under which its API speaks a family's exact shape.
//
// A table rather than a catalog column, and deliberately: an endpoint's path is
// the one part of a request the sandboxed core may never influence (decision
// 0049), so a served document that could name a prefix would be a served
// document that could move a request. The core keeps sending an origin; this
// process keeps owning every path; a vendor's prefix is compiled here beside
// the paths it is joined to.
//
// A provider absent from this table has no prefix, which is the ordinary case.
//
// `version` is the second half of the same problem and exists because one
// vendor made it visible. A family's path is a version segment followed by an
// operation — `/v1` + `/messages`, `/v1` + `/chat/completions` — and most
// vendors serve the version the family names. Z.AI serves the same
// OpenAI-shaped API at `/api/paas/v4`. With only a prefix column this table
// would have composed `/api/paas/v1/chat/completions`, which is not an address
// that vendor answers: a row that reads as "this provider is reachable" while
// naming somewhere it is not is worse than no row, because the failure it
// produces is a plausible 404 rather than a missing entry anybody notices.
//
// An empty `version` means the family's own, which is every row but two. The
// second is DeepInfra, and it is also the one row with an empty *prefix*: that
// vendor documents its OpenAI-shaped API at `/v1/openai`, so what stands where
// the family's `/v1` would is two segments rather than one. Writing it as the
// version rather than as a prefix keeps the column meaning one thing — the
// segments a vendor serves this family's operations beneath — instead of
// splitting one address across two columns that would then have to be read
// together to see what it composes to.
struct ProviderPathPrefix {
  std::string_view provider_id;
  std::string_view prefix;
  std::string_view version;
};

constexpr ProviderPathPrefix kProviderPathPrefixes[] = {
    {"deepinfra", "", "/v1/openai"},
    {"fireworks", "/inference", ""},
    {"groq", "/openai", ""},
    {"kilocode", "/api/gateway", ""},
    {"kimi-coding", "/coding", ""},
    {"minimax-cn", "/anthropic", ""},
    {"novita", "/openai", ""},
    {"opencode", "/zen", ""},
    {"opencode-go", "/zen/go", ""},
    {"openrouter", "/api", ""},
    {"qwen", "/compatible-mode", ""},
    {"qwen-token-plan", "/compatible-mode", ""},
    {"qwen-token-plan-cn", "/compatible-mode", ""},
    {"venice", "/api", ""},
    {"zai", "/api/paas", "/v4"},
    {"zai-coding-cn", "/api/coding/paas", "/v4"},
};

// The compiled base for one provider: its prefix and its version override.
//
// Returned as the row rather than as two lookups, so a caller cannot read one
// vendor's prefix and another's version — which is the shape of mistake a
// second table walk would eventually make.
ProviderPathPrefix PathBaseFor(std::string_view provider_id) {
  for (const ProviderPathPrefix &entry : kProviderPathPrefixes) {
    if (entry.provider_id == provider_id) {
      return entry;
    }
  }
  return ProviderPathPrefix{provider_id, std::string_view(),
                            std::string_view()};
}

// The version segment a family's path should carry for this provider.
std::string_view VersionOr(const ProviderPathPrefix &path_base,
                           std::string_view family_version) {
  return path_base.version.empty() ? family_version : path_base.version;
}

// One family's path, in the two forms the two kinds of endpoint need.
//
// The prefix and the version are facts about where a *vendor* put its copy of
// the family's API. A person who typed their own server's address has already
// said where they put theirs — that is what the base URL is — so neither of
// those two pieces is theirs to be given, and only the operation is joined
// beneath what they typed.
//
// Both forms come out of one call so the operation is written once. A second
// literal beside the first would be a family whose two spellings could drift,
// and the drift would show up as a person's own server being sent to a route
// invented for somebody else's.
struct ComposedPath {
  // Under a catalog origin: everything below it.
  std::string from_origin;
  // Under a person's base URL: the operation, with no leading separator, so
  // that joining it beneath a base path is an append rather than a
  // replacement.
  std::string from_base;
};

ComposedPath ComposePath(std::string_view prefix, std::string_view version,
                         std::string_view operation) {
  // Every operation below is written with its leading separator, because that
  // is how the composed path reads. The base-relative form is the same string
  // without it, and the empty case is guarded rather than assumed away: a
  // family added later with nothing to say would otherwise index past the end
  // of a string, which is a crash in the browser process over a typo.
  const std::string_view beneath =
      operation.empty() ? operation : operation.substr(1);
  return ComposedPath{base::StrCat({prefix, version, operation}),
                      std::string(beneath)};
}

// One request URL under a catalog origin.
//
// Unchanged from when this was the only kind there was, and deliberately kept
// as its own function rather than folded into the one below: the catalog rule
// requires https and composes from the origin, the register rule does neither,
// and decision 0096 section 2 is explicit that relaxing either must not relax
// the other. Two functions cannot be relaxed by one edit.
std::optional<GURL> UnderCatalogOrigin(const GURL &origin,
                                       const std::string &path) {
  const GURL url = origin.Resolve(path);
  // The endpoint has already been checked to be an https origin with no path,
  // and the path is composed here from a compiled family route and, at most, a
  // compiled vendor prefix — never from anything the core sent — so resolving
  // it cannot move the host. Checking it anyway is what keeps that a property
  // of this function: a caller that hands it something else gets nothing back,
  // rather than a URL whose safety was an argument about a different function.
  if (!url.is_valid() || !url.SchemeIs(url::kHttpsScheme) ||
      url.DeprecatedGetOriginAsURL() != origin.DeprecatedGetOriginAsURL()) {
    return std::nullopt;
  }
  return url;
}

// One request URL beneath a person's own base URL.
//
// Appended to the spec with a separator ensured, which is
// `CustomEndpointProber::ListingUrl` next door doing the same thing for the
// same reason. `GURL::Resolve` is the tempting call and the wrong one: it
// reads a base path with no trailing separator as a file and drops its last
// segment, so `http://box:8000/v1` would resolve `chat/completions` to
// `http://box:8000/chat/completions` — an address that is not wrong in any way
// a person could see, because a server answers it with a plausible 404 rather
// than an error anybody reads. Ensuring the separator makes the two spellings
// a person might type, with and without it, the same request.
//
// A base carrying a query or a fragment is refused rather than appended to,
// and that refusal is this function's own rather than a restatement of the
// address policy. The policy decides what may be *registered*; this decides
// what can be joined beneath, and the two happen to agree about queries for
// different reasons. Appending to `http://box:8000/v1?x` would produce
// `http://box:8000/v1?x/chat/completions`, whose path is `/v1` and whose query
// swallowed the operation — same origin, wrong request, and a server would
// answer it rather than object to it.
//
// The result is then checked to be beneath the base and not merely on the same
// host: same scheme, host and port, and a path that still starts with the
// base's. A join that escaped either is a bug rather than a request.
std::optional<GURL> UnderUserBase(const GURL &base_url,
                                  const std::string &relative_path) {
  // Asked before `spec()`, which DCHECKs on a URL that did not parse.
  std::string spec = base_url.is_valid() ? base_url.spec() : std::string();
  if (spec.empty() || base_url.has_query() || base_url.has_ref()) {
    return std::nullopt;
  }
  if (spec.back() != '/') {
    spec.push_back('/');
  }
  const GURL url(base::StrCat({spec, relative_path}));
  if (!url.is_valid() ||
      url.DeprecatedGetOriginAsURL() != base_url.DeprecatedGetOriginAsURL() ||
      // `path()`, not `path_piece()`: at the pinned Chromium the bare name is
      // the non-copying accessor and the `_piece()` spellings are gone
      // (url/gurl.h at 152.0.7977.42, and the same note in
      // scrubbing_serializer.cc).
      !base::StartsWith(url.path(), base_url.path(),
                        base::CompareCase::SENSITIVE)) {
    return std::nullopt;
  }
  return url;
}

// Header names this transport composes itself.
//
// Refused to the core for a different reason than the credential names in
// `core_model_effect_validation.cc` are: those are about a secret, and these
// are about who builds the request. `Content-Type` is set from the route
// table when the body is attached, and a second one arriving from the core
// would either be silently overwritten — the core having proposed something
// that did not happen — or would meet the upload attachment's own expectation
// that it is the only party setting it.
//
// It lives here rather than in the contract's validator because it is a fact
// about *this* transport. A future streaming transport composes a different
// set, and a rule in the shared validator would then be refusing headers for a
// request shape it knows nothing about.
constexpr std::string_view kBrokerOwnedHeaderNames[] = {
    "accept",
    "content-length",
    "content-type",
    "host",
    // Not composed here at all, and refused all the same. A `User-Agent` is a
    // claim about which product is making the request; the core is not the
    // party that gets to make it, and a served overlay reaching the core
    // through `static_headers` is even less so.
    "user-agent",
    // The three a family composes for itself. They are refused to the core
    // for the same reason the media type is: this transport writes them, and
    // a second one arriving from the core would either be overwritten — the
    // core having proposed something that did not happen — or would let the
    // core state a client identity to a vendor, which is a claim about who is
    // calling and not the core's to make.
    //
    // The last of them is stronger than that. It is read out of the
    // credential's own payload, and the credential never enters the core at
    // all, so a core that named an account would be naming one it could not
    // have read — which is either a guess or a value from somewhere else.
    "openai-beta",
    "originator",
    "chatgpt-account-id",
};

}  // namespace

// static
std::optional<ModelRoute> ProfileModelBroker::RouteFor(
    service::ProviderWireApi wire_api,
    std::string_view provider_id,
    std::string_view model_id) {
  // Joined at the front of whatever the family composes, so the family's own
  // shape is written once and a vendor's prefix cannot change it. The version
  // segment is the one part a vendor may replace, and it is replaced by name
  // rather than by rewriting the composed string, so the operation half stays
  // the family's alone.
  // Not named `base`: `base::StrCat` is composed a few lines down and a local
  // of that name would shadow the namespace.
  const ProviderPathPrefix path_base = PathBaseFor(provider_id);
  const std::string_view prefix = path_base.prefix;
  // Bearer wherever a family reads one, and a family-specific header where it
  // reads something else, because that is what each provider reads. Every one
  // of those names is on the deny list `core_model_effect_validation.cc`
  // applies to static headers, and that overlap is the whole mechanism: the
  // core cannot write any of them, so the credential reaches a provider by
  // exactly one route — resolved here, from the profile's secure store,
  // against a handle.
  constexpr CredentialPlacement kBearer = {"authorization", "Bearer "};

  // Every family posts JSON today. It is a column rather than a constant so
  // that a family that does not has somewhere to say so, next to the path it
  // would also have to add.
  constexpr std::string_view kJson = "application/json";

  switch (wire_api) {
    case service::ProviderWireApi::kAnthropicMessages: {
      const ComposedPath path =
          ComposePath(prefix, VersionOr(path_base, "/v1"), "/messages");
      return ModelRoute{path.from_origin,
                        path.from_base,
                        std::string(kJson),
                        {"x-api-key", ""}};
    }
    case service::ProviderWireApi::kOpenAiResponses: {
      const ComposedPath path =
          ComposePath(prefix, VersionOr(path_base, "/v1"), "/responses");
      return ModelRoute{path.from_origin, path.from_base, std::string(kJson),
                        kBearer};
    }
    case service::ProviderWireApi::kOpenAiCompletions: {
      const ComposedPath path =
          ComposePath(prefix, VersionOr(path_base, "/v1"), "/chat/completions");
      return ModelRoute{path.from_origin, path.from_base, std::string(kJson),
                        kBearer};
    }
    case service::ProviderWireApi::kGoogleGenerativeLanguage: {
      // The one family that puts the model in the path. This browser refuses
      // a model id it cannot spell there, rather than sending the request to
      // whatever route the escaped form happened to produce.
      //
      // The key also travels as a header here even though this family accepts
      // it as a query parameter. A query string is written to proxy logs, and
      // a credential in one is a credential leaked to every intermediary the
      // request passed through.
      if (!IsPathSafeModelId(model_id)) {
        return std::nullopt;
      }
      const ComposedPath path = ComposePath(
          prefix, VersionOr(path_base, "/v1beta"),
          base::StrCat({"/models/", model_id, ":generateContent"}));
      return ModelRoute{path.from_origin,
                        path.from_base,
                        std::string(kJson),
                        {"x-goog-api-key", ""}};
    }
    case service::ProviderWireApi::kManaged: {
      // The product's own canonical schema, spoken only to the compiled
      // worker origin. The bearer here is never a stored provider secret: it
      // is the single-use entitlement token the browser minted, attached
      // through the same placement machinery so there is still exactly one
      // way a credential-shaped value enters a request.
      //
      // Its base-relative form is composed like every other family's and is
      // never used: `Dispatch` refuses a managed effect that claims anything
      // but a catalog origin, and refuses one whose endpoint is not the
      // compiled worker origin. It is filled in rather than left empty so
      // that no reader has to work out whether an empty one would have meant
      // "the origin" or "nothing at all".
      const ComposedPath path = ComposePath("", "/v1", "/messages");
      return ModelRoute{path.from_origin, path.from_base, std::string(kJson),
                        kBearer};
    }
    case service::ProviderWireApi::kOpenAiCodexResponses: {
      // One vendor's subscription endpoint, reached with a subscription
      // credential at an address that is not the platform one. It is a family
      // of its own rather than a flag on the responses route because the
      // platform endpoint refuses what this one requires; sending either
      // shape to the other's path produces a rejection nobody can read.
      //
      // No vendor prefix and no version segment are joined here. A
      // subscription endpoint is not a vendor's public API base, and the two
      // are configured separately, so borrowing either column would compose an
      // address neither the catalog nor this table states.
      // Defined here rather than beside the deny list above so the table sits
      // with the one family that reads it. Its storage duration is static all
      // the same, which is what lets a route hold a span of it.
      // Both are the borrowed tool's own headers in shape, and one of them
      // is not in value. The experimental opt-in is this endpoint's
      // requirement, unchanged. The originator names TaffyGo rather than the
      // tool whose client identity the sign-in borrowed (decision 0111
      // section 1): an originator is a claim about which product is making
      // the request, and decision 0095 section 4 already commits this product
      // to telling the person whose client they authorized, which is hard to
      // square with telling the vendor something else. The cost is stated
      // rather than hidden — an authorization server that only recognises its
      // own originator may refuse this, and only a device attempt settles it
      // — and it is one value in one table if it does.
      //
      // A `session-id` belongs beside these and is not here: it is
      // per-conversation rather than per-family, so the core derives it and
      // sends it as a static header, and the hyphen in its name is
      // load-bearing (decision 0111 section 3).
      static constexpr FamilyHeader kHeaders[] = {
          {"openai-beta", "responses=experimental"},
          {"originator", "taffygo"},
      };
      const ComposedPath path =
          ComposePath("", "", "/backend-api/codex/responses");
      // The account the subscription belongs to, named to the vendor that
      // issued the credential (decision 0111 section 4). The namespace is a
      // URL and is one key rather than a path, which is why it is carried
      // whole.
      constexpr CredentialClaimHeader kAccount = {
          "chatgpt-account-id", "https://api.openai.com/auth",
          "chatgpt_account_id"};
      return ModelRoute{path.from_origin, path.from_base, std::string(kJson),
                        kBearer,          kHeaders,       kAccount};
    }
    case service::ProviderWireApi::kGoogleCloudCodeAssist: {
      // Another vendor's subscription endpoint, and the second family whose
      // address is not the vendor's public API base. No prefix and no version
      // segment for the same reason the row above joins neither: the operation
      // is the whole path this endpoint serves, and the vendor's own
      // generative-language base has nothing to do with it.
      //
      // The first route to carry a query, and it is part of the operation
      // rather than a parameter this browser chose. The endpoint always
      // streams; the query selects which framing it streams in, and without it
      // the reply is a JSON array of frames that the reply reader would find
      // no answer in. It survives both joins untouched — the catalog rule
      // resolves the path against the origin and compares origins, and the
      // register rule compares `path()`, which excludes a query — so no column
      // is needed to carry it.
      //
      // The model is in the body on this family, not in the path, so there is
      // no model id to spell safely here.
      //
      // Deliberately no `User-Agent`. Both reference implementations send the
      // borrowed tool's own, and this product has refused that claim three
      // times already — see the `originator` note above and decisions 0111
      // section 1 and 0095 section 4. Whether this endpoint requires one is a
      // question only a device attempt answers, and it is recorded as one
      // rather than settled by sending somebody else's product name.
      const ComposedPath path =
          ComposePath("", "", "/v1internal:streamGenerateContent?alt=sse");
      return ModelRoute{path.from_origin, path.from_base, std::string(kJson),
                        kBearer};
    }
  }
  NOTREACHED();
}

// static
std::optional<GURL> ProfileModelBroker::ResolveUrl(
    service::ModelEndpointKind endpoint_kind,
    const std::string& endpoint,
    const ModelRoute& route) {
  const GURL parsed(endpoint);
  // Two joins, chosen by the authority the request claimed, and written so
  // that neither is expressed in terms of the other (decision 0096 section 2).
  // The catalog arm is the one this function has always had; the register arm
  // is new and shares nothing with it but the origin comparison at the end of
  // each, which both make for themselves.
  switch (endpoint_kind) {
    case service::ModelEndpointKind::kCatalogOrigin:
      return UnderCatalogOrigin(parsed, route.path);
    case service::ModelEndpointKind::kUserBaseUrl:
      return UnderUserBase(parsed, route.base_relative_path);
  }
  NOTREACHED();
}

// static
ProfileModelBroker::CredentialOriginOutcome
ProfileModelBroker::ResolveCredentialOrigin(
    const service::ModelRequestEffect& request,
    const std::optional<std::string>& credential_origin,
    const ModelRoute& route) {
  // Only a catalog address is ever replaced. There is no third kind of
  // endpoint here and deliberately so: the request still claims the catalog
  // authority, is still judged by the catalog rule, and what this does is
  // narrow inside the answer that rule already gave. A person's own
  // registered address is left exactly as they typed it, so decision 0096
  // section 2's two rules stay independent — neither has been relaxed, and
  // neither has been written in terms of the other.
  //
  // What a credential's own address decides for a registered address is not
  // *where* the request goes but *whether* the credential is spent there
  // (decision 0116). The register is the only authority over the address, so
  // it is never rewritten; the credential's issuer is the only authority over
  // where the credential may be spent, so a credential that named its own host
  // is spent at a registered address only when that address sits at or beneath
  // the domain the vendor's row licenses — the same predicate the catalog arm
  // below asks of an issued host. Outside it the pairing is refused by name,
  // with the same closed denial the licence check answers a catalog request
  // with, rather than the credential's host being dropped in silence. A
  // credential that named no host is unaffected: it never had an address to be
  // spent beneath, and the register's address is where it has always gone.
  //
  // The predicate is asked about the registered address's origin rather than
  // the address itself. A registered address is a base URL and ordinarily
  // carries a path, and the licence check accepts only the one canonical
  // spelling of an https origin; the origin is what the licence is a statement
  // about, and an address that has no origin — cleartext, or a string that is
  // not a URL — has nothing the row could have licensed and is refused.
  if (request.endpoint_kind == service::ModelEndpointKind::kUserBaseUrl) {
    if (!credential_origin) {
      return {CredentialOriginVerdict::kUnchanged, GURL()};
    }
    const std::string registered_origin =
        url::Origin::Create(GURL(request.endpoint)).Serialize();
    return {ProviderCredentialHostLicensed(request.provider_id,
                                           registered_origin)
                ? CredentialOriginVerdict::kUnchanged
                : CredentialOriginVerdict::kRefused,
            GURL()};
  }
  if (request.endpoint_kind != service::ModelEndpointKind::kCatalogOrigin) {
    return {CredentialOriginVerdict::kUnchanged, GURL()};
  }
  if (!credential_origin) {
    // The two silences, told apart by this binary's own vendor table rather
    // than by each other — which is the whole distinction, because they arrive
    // here identical. A vendor that issues no address never named one, and the
    // catalog's own is where its requests have always gone. A vendor that does
    // issue one has lost it somewhere behind this call:
    // `ParseSecondaryTokenResponse` keeps the token and drops an address
    // outside the licensed domain, a record sealed before the row existed
    // carries none, and a store that answered from either leaves nothing to
    // send to. The catalog origin is not a fallback there — it is the domain
    // the row licenses, not an endpoint.
    return {ProviderCredentialNamesItsOwnHost(request.provider_id)
                ? CredentialOriginVerdict::kRefused
                : CredentialOriginVerdict::kUnchanged,
            GURL()};
  }
  // An address arrived, so one was called for whatever the row says, and every
  // path from here either uses it or refuses the call. An empty string needs no
  // case of its own: it is not a canonical https origin and the licence check
  // is where it is refused.
  if (!ProviderCredentialHostLicensed(request.provider_id,
                                      *credential_origin) ||
      !ProviderCredentialHostAddresses(request.endpoint, *credential_origin)) {
    return {CredentialOriginVerdict::kRefused, GURL()};
  }
  // Composed by the catalog arm, from the same compiled path. A substitution
  // that built its own URL would be a second place the path could come from,
  // and the one property this whole subsystem rests on is that the path has
  // exactly one source.
  const std::optional<GURL> substituted =
      UnderCatalogOrigin(GURL(*credential_origin), route.path);
  if (!substituted) {
    return {CredentialOriginVerdict::kRefused, GURL()};
  }
  return {CredentialOriginVerdict::kSubstituted, *substituted};
}

// static
bool ProfileModelBroker::StaticHeadersAreCarryable(
    const std::vector<service::ModelStaticHeaderPtr>& headers) {
  for (const service::ModelStaticHeaderPtr& header : headers) {
    if (!header) {
      return false;
    }
    const std::string lowered = base::ToLowerASCII(header->name);
    for (std::string_view owned : kBrokerOwnedHeaderNames) {
      if (lowered == owned) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace taffy
