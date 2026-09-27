// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The endpoint probe's transport.
//
// Every loader setting here is the model broker's, copied rather than chosen
// again: no cache in either direction, no ambient cookies, no redirect
// followed, no transport retry, and a bounded download. The redirect refusal
// is the one worth restating — following one would send a person's credential
// wherever a server on their own network pointed, and this request exists
// precisely because nothing has been established about that server yet.

#include "taffy/browser/model/custom_endpoint_prober.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

// Ollama's and llama.cpp's own paths, at the server root.
constexpr char kOllamaTagsPath[] = "/api/tags";
constexpr char kLlamaCppPropsPath[] = "/props";

// One attempt's allowance. A server on the same network answers a listing in
// milliseconds; this is long enough for one that has just started and short
// enough that three of them are still a wait a person will sit through.
constexpr base::TimeDelta kProbeTimeout = base::Seconds(15);

void ClearProbeString(std::string* value) {
  if (!value) {
    return;
  }
  std::ranges::fill(*value, '\0');
  value->clear();
}

}  // namespace

CustomEndpointProber::CustomEndpointProber(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

CustomEndpointProber::~CustomEndpointProber() {
  if (credential_) {
    ClearProbeString(&*credential_);
  }
}

bool CustomEndpointProber::probing_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !callback_.is_null();
}

// static
GURL CustomEndpointProber::ListingUrl(const GURL& endpoint) {
  std::string spec = endpoint.spec();
  if (spec.empty()) {
    return GURL();
  }
  if (spec.back() != '/') {
    spec.push_back('/');
  }
  const GURL url(base::StrCat({spec, "models"}));
  // The endpoint has already been registered, so it carries no query and no
  // fragment and appending cannot have produced either. Checking that the
  // origin survived is what keeps that a property of this function rather than
  // an argument about a different one.
  if (!url.is_valid() ||
      url.DeprecatedGetOriginAsURL() != endpoint.DeprecatedGetOriginAsURL()) {
    return GURL();
  }
  return url;
}

// static
GURL CustomEndpointProber::OpenAiBaseAtOrigin(const GURL& endpoint) {
  const GURL origin = endpoint.DeprecatedGetOriginAsURL();
  if (!origin.is_valid()) {
    return GURL();
  }
  // The origin's spec always ends in `/`, so appending rather than resolving
  // is both correct and the same rule `ListingUrl` follows: a resolve would be
  // a second way to build an address, and two ways to build one address is how
  // they stop agreeing.
  const GURL base(base::StrCat({origin.spec(), "v1"}));
  if (!base.is_valid() || base.DeprecatedGetOriginAsURL() != origin) {
    return GURL();
  }
  return base;
}

// static
GURL CustomEndpointProber::RuntimeUrl(const GURL& endpoint,
                                      const std::string& path) {
  const GURL url = endpoint.DeprecatedGetOriginAsURL().Resolve(path);
  if (!url.is_valid() ||
      url.DeprecatedGetOriginAsURL() != endpoint.DeprecatedGetOriginAsURL()) {
    return GURL();
  }
  return url;
}

void CustomEndpointProber::Probe(const GURL& endpoint,
                                 std::optional<std::string> credential,
                                 AnswerCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback_.is_null() || !endpoint.is_valid() ||
      !url_loader_factory_) {
    if (credential) {
      ClearProbeString(&*credential);
    }
    std::move(callback).Run(Answer());
    return;
  }
  endpoint_ = endpoint;
  credential_ = std::move(credential);
  callback_ = std::move(callback);
  listing_ = OpenAiListingReading();
  Start(Step::kOpenAiListing);
}

void CustomEndpointProber::Start(Step step) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  GURL url;
  switch (step) {
    case Step::kOpenAiListing:
      url = ListingUrl(endpoint_);
      break;
    case Step::kOllamaTags:
      url = RuntimeUrl(endpoint_, kOllamaTagsPath);
      break;
    case Step::kLlamaCppProps:
      url = RuntimeUrl(endpoint_, kLlamaCppPropsPath);
      break;
  }
  if (!url.is_valid()) {
    Advance(step);
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = net::HttpRequestHeaders::kGetMethod;
  resource_request->load_flags =
      net::LOAD_DISABLE_CACHE | net::LOAD_BYPASS_CACHE;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->redirect_mode = network::mojom::RedirectMode::kError;
  resource_request->headers.SetHeader(net::HttpRequestHeaders::kAccept,
                                      "application/json");
  if (credential_) {
    // One placement, because there is one convention every server in this set
    // reads: the OpenAI-compatible bearer header. It travels on the runtime
    // paths too — a self-hosted server sitting behind a proxy that wants a key
    // wants it on every path, and a probe that skipped it there would report
    // an address unreachable that a save would then accept.
    resource_request->headers.SetHeader(
        net::HttpRequestHeaders::kAuthorization,
        base::StrCat({"Bearer ", *credential_}));
  }

  constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
      net::DefineNetworkTrafficAnnotation("taffy_custom_endpoint_probe", R"(
        semantics {
          sender: "TaffyGo endpoint probe"
          description: "Asks the model server a person typed the address of "
                       "what kind of server it is and which models it offers."
          trigger: "The person entered the address of their own model server "
                   "and asked TaffyGo to check it before saving it."
          data: "No body and no page content. If the person supplied a key "
                "for that server, it travels in one header."
          destination: OTHER
          internal { contacts { owners: "//taffy/OWNERS" } }
          user_data {
            type: ACCESS_TOKEN
          }
          last_reviewed: "2026-08-29"
        }
        policy {
          cookies_allowed: NO
          setting: "The person types the address and asks for the check. With "
                   "none typed, no request is made."
          policy_exception_justification: "Not implemented."
        })");

  auto loader = network::SimpleURLLoader::Create(std::move(resource_request),
                                                 kTrafficAnnotation);
  // An error status is an answer about the path, so the body is read rather
  // than dropped and the status is consulted below.
  loader->SetAllowHttpErrorResults(true);
  loader->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader->SetTimeoutDuration(kProbeTimeout);

  network::SimpleURLLoader* raw_loader = loader.get();
  loader_ = std::move(loader);
  raw_loader->DownloadToString(
      url_loader_factory_.get(),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&CustomEndpointProber::OnResponse,
                         weak_factory_.GetWeakPtr(), step),
          std::nullopt),
      static_cast<size_t>(service::kMaxProviderListingBytes));
}

void CustomEndpointProber::OnResponse(
    Step step,
    std::optional<std::string> response_body) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!loader_ || callback_.is_null()) {
    return;
  }
  const int net_error = loader_->NetError();
  int http_status = 0;
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  if (head && head->headers) {
    http_status = head->headers->response_code();
  }
  loader_.reset();

  const bool answered = net_error == net::OK && response_body &&
                        http_status >= 200 && http_status < 300;
  if (!answered) {
    Advance(step);
    return;
  }

  switch (step) {
    case Step::kOpenAiListing:
      listing_ = ReadOpenAiListing(*response_body);
      if (listing_.understood && listing_.names_vllm) {
        // vLLM named itself in the listing it just served, so there is
        // nothing left to ask.
        Finish(Reached(service::ServerKind::kVllm, listing_.model_count));
        return;
      }
      break;
    case Step::kOllamaTags: {
      const std::optional<uint32_t> tagged = ReadOllamaTags(*response_body);
      if (tagged) {
        // The listing's count wins when there was one, because that is the
        // roster the product would route against; the tag count stands only
        // when the OpenAI path said nothing.
        Finish(Reached(service::ServerKind::kOllama,
                       listing_.understood ? listing_.model_count : *tagged));
        return;
      }
      break;
    }
    case Step::kLlamaCppProps:
      if (ReadLlamaCppProps(*response_body)) {
        // `/props` carries no model list, so the count is whatever the
        // listing said — nothing, when it said nothing.
        Finish(Reached(service::ServerKind::kLlamaCpp, listing_.model_count));
        return;
      }
      break;
  }
  Advance(step);
}

// One reached verdict, assembled in the one place that knows both halves.
//
// The models are the listing's and only the listing's: `/api/tags` and
// `/props` are asked to let a server name itself, and neither answers in the
// shape a request could be routed against. So a runtime identified natively
// with no listing behind it is reached, named, and carries no roster — which
// is the honest answer and the one a setup screen can act on.
//
// The proved base follows from the same fact. A listing that answered proves
// the base it was asked at; a native path that answered proves only the
// server root, and the OpenAI-shaped API of every runtime in this set is at
// `<origin>/v1` beneath it.
CustomEndpointProber::Answer CustomEndpointProber::Reached(
    service::ServerKind server_kind,
    uint32_t model_count) const {
  Answer answer;
  answer.reached = true;
  answer.server_kind = server_kind;
  answer.model_count = model_count;
  answer.models = listing_.models;
  answer.proved_base = listing_.understood ? ProvedBase::kProbedBase
                                           : ProvedBase::kOriginV1;
  return answer;
}

void CustomEndpointProber::Advance(Step finished) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  switch (finished) {
    case Step::kOpenAiListing:
      Start(Step::kOllamaTags);
      return;
    case Step::kOllamaTags:
      Start(Step::kLlamaCppProps);
      return;
    case Step::kLlamaCppProps:
      break;
  }
  // Nothing named a runtime. A server that served the OpenAI listing is
  // reached and OpenAI-compatible, which is the honest answer rather than a
  // guess; one that served nothing at all was not reached.
  if (listing_.understood) {
    Finish(Reached(service::ServerKind::kOpenaiCompatible,
                   listing_.model_count));
    return;
  }
  Finish(Answer());
}

void CustomEndpointProber::Finish(Answer answer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loader_.reset();
  if (credential_) {
    ClearProbeString(&*credential_);
    credential_.reset();
  }
  AnswerCallback callback = std::move(callback_);
  if (callback) {
    std::move(callback).Run(answer);
  }
}

}  // namespace taffy
