// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_H_
#define TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/browser/model/custom_endpoint_prober_readings.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace taffy {

// The browser-process fetch that answers "what is at this address?" (decision
// 0096 section 5).
//
// It sits beside decision 0083's probe and asks a different question. That one
// proves a credential by spending one bounded completion; this one proves an
// address by asking the server to describe itself, and the verdict says what
// runtime it named and how many models it listed.
//
// **Zero models is an answer.** An address that answers with an empty listing
// is right, and nothing is loaded behind it; that is a sentence a person can
// act on, and it is reported as reached with a count of zero rather than as a
// failure.
//
// **Nothing here logs**, for the model broker's reason: the address is a fact
// about a person's own network and the credential is a credential.
class CustomEndpointProber final {
 public:
  // Where the OpenAI-shaped API was proved to be (decision 0096 section 5).
  //
  // This matters and is easy to get wrong. The transport joins only the
  // *operation* beneath a person's base, so `http://box:11434/v1` routes and a
  // bare `http://box:11434` routes to `/chat/completions`, which no runtime
  // serves — while the native fallbacks below succeed on that bare origin and
  // would otherwise have reported it as working. The prober is the one party
  // that knows which of the two happened, so it says so instead of leaving a
  // save to file an address that was never proved.
  //
  // Nothing is rewritten here or anywhere downstream. What this produces is a
  // proposal the surface shows and the person accepts, which is what keeps
  // decision 0096 section 1's "did this person type this?" true.
  enum class ProvedBase {
    // Nothing was proved. Either the address answered nowhere, or — for a
    // caller that reads this without checking `reached` — there is no claim
    // being made.
    kNothing,
    // The OpenAI-shaped listing answered at the base that was probed, so that
    // base is the proved one and it is the string the person typed.
    kProbedBase,
    // A native path identified the runtime at the server root instead, and the
    // listing did not answer. Every runtime in this set — Ollama, LM Studio,
    // vLLM and llama.cpp — serves its OpenAI-shaped API at `<origin>/v1`. That
    // is a compiled fact about each runtime rather than a guess about the
    // address.
    kOriginV1,
  };

  // What the address turned out to be.
  //
  // `server_kind` is meaningless unless `reached`, and the caller must not
  // read it otherwise — an unreached address named no runtime, and
  // `kOpenaiCompatible` is this struct's zero value rather than a finding.
  //
  // `model_count` and `models` are separate facts on purpose: the count is
  // what the server named and the list is what survived the bound and the
  // identity rule, so a list shorter than the count is a truncation the
  // surface can say out loud rather than one it cannot see.
  struct Answer {
    bool reached = false;
    core_service::mojom::ServerKind server_kind =
        core_service::mojom::ServerKind::kOpenaiCompatible;
    uint32_t model_count = 0;
    std::vector<CustomModelReading> models;
    ProvedBase proved_base = ProvedBase::kNothing;
  };
  using AnswerCallback = base::OnceCallback<void(Answer)>;

  explicit CustomEndpointProber(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  CustomEndpointProber(const CustomEndpointProber&) = delete;
  CustomEndpointProber& operator=(const CustomEndpointProber&) = delete;
  ~CustomEndpointProber();

  // Asks one address what it is. `callback` runs exactly once, unless this
  // object is destroyed first — a probe answers a person who is still
  // looking at the screen it belongs to, and there is nobody to tell once
  // that is gone.
  //
  // One probe at a time: a second while one is in flight is answered
  // unreached rather than queued, because a person waiting on a spinner has
  // asked one question and a queue would answer it twice.
  //
  // `credential` is material, not a handle — the caller has already spent
  // whatever handle it held — and it is cleared as soon as it is written into
  // a request.
  void Probe(const GURL& endpoint,
             std::optional<std::string> credential,
             AnswerCallback callback);

  bool probing_for_testing() const;

  // `<origin>/v1` for an endpoint, or an invalid GURL when it has no usable
  // origin. It is the address `ProvedBase::kOriginV1` names, composed here so
  // that the one place which knows the runtimes' compiled shape is also the
  // one place that writes it down; the caller composes the string for the
  // other arm out of what the person actually typed, which this class was
  // never given.
  static GURL OpenAiBaseAtOrigin(const GURL& endpoint);

 private:
  // The three questions, asked in this order. The OpenAI-shaped listing is
  // first because it is the one every server in the set serves and the only
  // one that carries a model count this product can route against; the two
  // runtime paths follow and are asked only to let a server name itself.
  enum class Step {
    kOpenAiListing,
    kOllamaTags,
    kLlamaCppProps,
  };

  void Start(Step step);
  void OnResponse(Step step, std::optional<std::string> response_body);
  void Advance(Step finished);
  void Finish(Answer answer);
  // One reached verdict, carrying the listing's roster and the base whichever
  // step answered actually proved.
  Answer Reached(core_service::mojom::ServerKind server_kind,
                 uint32_t model_count) const;

  // `<endpoint>/models`, built by appending rather than by resolving: a base
  // URL with a path — the `http://host:11434/v1` this whole record exists for
  // — loses its last segment to GURL::Resolve, which would silently ask a
  // different server than the one a person typed.
  static GURL ListingUrl(const GURL& endpoint);
  // A runtime path, which lives at the server root rather than under the base
  // path. Nothing but the origin of the endpoint is used, so a base path
  // cannot escape into it.
  static GURL RuntimeUrl(const GURL& endpoint, const std::string& path);

  const scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;

  GURL endpoint_;
  std::optional<std::string> credential_;
  AnswerCallback callback_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  // What the OpenAI listing said, carried across the runtime steps: whichever
  // runtime names itself, the count that reaches the core is the one from the
  // listing the product would actually route against.
  OpenAiListingReading listing_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<CustomEndpointProber> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_CUSTOM_ENDPOINT_PROBER_H_
