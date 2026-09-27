// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_model_effect_validation.h"

#include <stdint.h>

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::ModelStaticHeaderPtr Header(const std::string& name,
                                   const std::string& value) {
  auto header = mojom::ModelStaticHeader::New();
  header->name = name;
  header->value = value;
  return header;
}

mojom::ModelRequestEffectPtr ModelRequest() {
  auto request = mojom::ModelRequestEffect::New();
  request->route_id = "managed.primary";
  request->model_id = "fixture-model";
  request->disclosure = mojom::DisclosureClass::kUserSelectedContent;
  request->request_body = {1, 0, 2, 7};
  request->max_output_bytes = 4096;
  request->task_id = "task-1";
  request->provider_id = "fixture-provider";
  request->wire_api = mojom::ProviderWireApi::kAnthropicMessages;
  request->endpoint = "https://provider.taffy.test";
  // Stated rather than left to value initialization: a field that says which
  // rule is being claimed must never arrive by default.
  request->endpoint_kind = mojom::ModelEndpointKind::kCatalogOrigin;
  request->credential_handle = "credential-handle-1";
  request->static_headers.push_back(Header("anthropic-version", "2023-06-01"));
  return request;
}

bool Accepted(const mojom::ModelRequestEffect& request) {
  return IsValidCoreModelRequest(request, mojom::kMaxIdentifierBytes,
                                 mojom::kMaxEffectBytes);
}

// One register with one row in it, which is all the byte comparison needs.
RegisteredEndpointLookup Register(const std::string& provider_id,
                                  const std::string& endpoint) {
  return base::BindRepeating(
      [](std::string registered_provider, std::string registered_endpoint,
         const std::string& asked) -> std::optional<std::string> {
        if (asked != registered_provider) {
          return std::nullopt;
        }
        return registered_endpoint;
      },
      provider_id, endpoint);
}

bool AcceptedBySender(const mojom::ModelRequestEffect& request,
                      const RegisteredEndpointLookup& registered) {
  return IsValidCoreModelRequest(request, mojom::kMaxIdentifierBytes,
                                 mojom::kMaxEffectBytes, registered);
}

TEST(CoreModelEffectValidationTest, AcceptsAnOriginAndAnOrdinaryHeader) {
  EXPECT_TRUE(Accepted(*ModelRequest()));
}

TEST(CoreModelEffectValidationTest, AcceptsARequestNoTaskOwns) {
  // A direct model call has no task, and that is not a defect: it is the one
  // thing an empty task_id means. Refusing it here would refuse the direct
  // surface rather than an ill-formed effect.
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->task_id.clear();
  EXPECT_TRUE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, AProbeIsTaskLessOrItIsNothing) {
  // Decision 0083: a probe that named a task would be a paid call a
  // cancellation could reach and the ledger never journalled as that task's
  // work. Task-less, the same effect is well formed.
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->probe = true;
  EXPECT_FALSE(Accepted(*request));
  request->task_id.clear();
  EXPECT_TRUE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, MediaIsAnExactTaskOwnedPageContentPair) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->disclosure = mojom::DisclosureClass::kPageContent;
  request->media_attachment_handle = "media-handle-1";
  request->media_attachment_mime_type = "image/png";
  EXPECT_TRUE(Accepted(*request));

  request->media_attachment_mime_type = std::nullopt;
  EXPECT_FALSE(Accepted(*request));
  request->media_attachment_mime_type = "image/jpeg";
  EXPECT_FALSE(Accepted(*request));
  request->media_attachment_mime_type = "image/png";
  request->disclosure = mojom::DisclosureClass::kUserSelectedContent;
  EXPECT_FALSE(Accepted(*request));
  request->disclosure = mojom::DisclosureClass::kPageContent;
  request->task_id.clear();
  EXPECT_FALSE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, RefusesAnythingButAnHttpsOrigin) {
  // Each of these parses, and each would reach a different request than the
  // origin alone: a route on the host, a query the provider reads, a fragment
  // the transport drops, a credential in the URL, and a scheme with no
  // transport security at all.
  for (const char* endpoint : {
           "https://provider.taffy.test/v1/messages",
           "https://provider.taffy.test/",
           "https://provider.taffy.test?key=secret",
           "https://provider.taffy.test#fragment",
           "https://user:password@provider.taffy.test",
           "https://provider.taffy.test:443",
           "https://Provider.Taffy.Test",
           "http://provider.taffy.test",
           "wss://provider.taffy.test",
           "provider.taffy.test",
           "",
       }) {
    mojom::ModelRequestEffectPtr request = ModelRequest();
    request->endpoint = endpoint;
    EXPECT_FALSE(Accepted(*request)) << endpoint;
  }
}

TEST(CoreModelEffectValidationTest, RefusesAHeaderACredentialTravelsIn) {
  for (const char* name : {
           "Authorization",
           "authorization",
           "proxy-authorization",
           "Cookie",
           "set-cookie",
           "x-api-key",
           "X-Api-Key",
           "api_key",
           "x-goog-api-key",
           "x-auth-token",
           "x-provider-secret",
       }) {
    mojom::ModelRequestEffectPtr request = ModelRequest();
    request->static_headers.push_back(Header(name, "value"));
    EXPECT_FALSE(Accepted(*request)) << name;
  }
}

TEST(CoreModelEffectValidationTest, RefusesAValueThatCouldStartAnotherHeader) {
  // The deny list above is only worth what this refusal is worth. A value
  // carrying a carriage return and a line feed ends its own header and begins
  // one of the sender's choosing, and that header's name is never a name the
  // deny list is asked about.
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->static_headers.push_back(
      Header("x-trace", "ok\r\nAuthorization: Bearer stolen"));
  EXPECT_FALSE(Accepted(*request));

  request = ModelRequest();
  request->static_headers.push_back(Header("x-trace", std::string("ok\0", 3)));
  EXPECT_FALSE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, RefusesAMalformedOrRepeatedHeaderName) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->static_headers.push_back(Header("x trace", "value"));
  EXPECT_FALSE(Accepted(*request));

  request = ModelRequest();
  request->static_headers.push_back(Header("", "value"));
  EXPECT_FALSE(Accepted(*request));

  request = ModelRequest();
  request->static_headers.push_back(Header("X-Trace", "one"));
  request->static_headers.push_back(Header("x-trace", "two"));
  EXPECT_FALSE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, RefusesHeadersPastTheDeclaredBound) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->static_headers.clear();
  for (uint64_t index = 0; index <= mojom::kMaxModelStaticHeaders; ++index) {
    request->static_headers.push_back(
        Header("x-trace-" + base::NumberToString(index), "value"));
  }
  EXPECT_FALSE(Accepted(*request));

  request = ModelRequest();
  request->static_headers.push_back(Header(
      "x-trace",
      std::string(static_cast<size_t>(mojom::kMaxModelHeaderValueBytes) + 1,
                  'a')));
  EXPECT_FALSE(Accepted(*request));
}

TEST(CoreModelEffectValidationTest, RefusesAnEmptyProviderOrCredentialHandle) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->provider_id.clear();
  EXPECT_FALSE(Accepted(*request));

  // Absent is a provider that needs no credential; present and empty is a
  // handle that resolves to nothing, which is a different thing entirely.
  request = ModelRequest();
  request->credential_handle.reset();
  EXPECT_TRUE(Accepted(*request));

  request = ModelRequest();
  request->credential_handle = "";
  EXPECT_FALSE(Accepted(*request));
}

// The catalog rule keeps every refusal it had, and it keeps them when a
// register is present: a request that claims the catalog is answered by the
// catalog rule and never by the register beside it. A path is the case worth
// naming, because it is the one thing a person's own address is allowed to
// carry and a catalog address still is not.
TEST(CoreModelEffectValidationTest, ACatalogRequestWithAPathIsStillRefused) {
  const RegisteredEndpointLookup registered =
      Register("fixture-provider", "https://provider.taffy.test/v1");

  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->endpoint = "https://provider.taffy.test/v1";
  EXPECT_FALSE(Accepted(*request));
  EXPECT_FALSE(AcceptedBySender(*request, registered));

  // And the register holding that exact string does not help it: the request
  // claimed the catalog, so the catalog rule is the one that answers it.
  request->endpoint = "http://192.168.1.9:11434/v1";
  EXPECT_FALSE(AcceptedBySender(
      *request, Register("fixture-provider", "http://192.168.1.9:11434/v1")));
}

// The register rule, in both directions. The address is accepted because this
// browser wrote it down for this provider, and for no other reason: a byte
// different, a provider different, and it is refused.
TEST(CoreModelEffectValidationTest, AUserAddressIsAcceptedOnlyByTheRegister) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->endpoint_kind = mojom::ModelEndpointKind::kUserBaseUrl;
  request->endpoint = "http://192.168.1.9:11434/v1";

  EXPECT_TRUE(AcceptedBySender(
      *request, Register("fixture-provider", "http://192.168.1.9:11434/v1")));

  // A trailing slash is a different string, and the register holds exactly
  // one. Validation would have called both acceptable; the register does not
  // answer "is this acceptable" at all.
  EXPECT_FALSE(AcceptedBySender(
      *request, Register("fixture-provider", "http://192.168.1.9:11434/v1/")));
  EXPECT_FALSE(AcceptedBySender(
      *request, Register("fixture-provider", "http://192.168.1.9:11435/v1")));
  EXPECT_FALSE(AcceptedBySender(
      *request, Register("another-provider", "http://192.168.1.9:11434/v1")));
}

// An address in nobody's register is refused, and so is one asked of a caller
// that holds no register at all. The register is the only authority that can
// accept a person's own address, so its absence is a refusal rather than a
// deferral in the one place that sends.
TEST(CoreModelEffectValidationTest, AnUnregisteredUserAddressIsRefused) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->endpoint_kind = mojom::ModelEndpointKind::kUserBaseUrl;
  request->endpoint = "http://192.168.1.9:11434/v1";

  EXPECT_FALSE(AcceptedBySender(*request, RegisteredEndpointLookup()));
  EXPECT_FALSE(AcceptedBySender(
      *request, Register("fixture-provider", "http://10.0.0.5:8000/v1")));
}

// A request claiming a person's own address is refused by the register rule
// even when its address would have passed the catalog rule. Claiming the
// wrong kind does not fall through to the other one.
TEST(CoreModelEffectValidationTest, AnHttpsOriginIsNotAutomaticallyRegistered) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->endpoint_kind = mojom::ModelEndpointKind::kUserBaseUrl;
  EXPECT_FALSE(AcceptedBySender(*request, RegisteredEndpointLookup()));
  EXPECT_TRUE(AcceptedBySender(
      *request, Register("fixture-provider", "https://provider.taffy.test")));
}

// The form a caller with no register asks bounds a person's own address and
// settles nothing about it. Nothing sends on this answer.
TEST(CoreModelEffectValidationTest, AUserAddressIsOnlyBoundedWithoutARegister) {
  mojom::ModelRequestEffectPtr request = ModelRequest();
  request->endpoint_kind = mojom::ModelEndpointKind::kUserBaseUrl;
  request->endpoint = "http://192.168.1.9:11434/v1";
  EXPECT_TRUE(Accepted(*request));

  request->endpoint.clear();
  EXPECT_FALSE(Accepted(*request));

  request->endpoint = std::string(
      static_cast<size_t>(mojom::kMaxProviderEndpointBytes) + 1u, 'a');
  EXPECT_FALSE(Accepted(*request));
}

}  // namespace
}  // namespace taffy
