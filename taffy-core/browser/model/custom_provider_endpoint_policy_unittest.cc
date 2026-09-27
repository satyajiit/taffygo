// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a person may register as their own model server's address.
//
// The cases that matter here are the ones a string pattern gets wrong.
// `http://0x7f.1/` is 127.0.0.1, `http://2130706433/` is 127.0.0.1, and
// `http://[::ffff:192.168.1.9]/` is a private address wearing IPv6 — none of
// them looks like what it is, and all three have to be accepted over plain
// http for the same reason `http://[::ffff:8.8.8.8]/` has to be refused: the
// address is decided by canonicalizing it, never by reading it.

#include "taffy/browser/model/custom_provider_endpoint_policy.h"

#include <string>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool MayRegister(const std::string &value) {
  return ClassifyCustomProviderEndpoint(value) ==
         CustomEndpointRefusal::kNone;
}

TEST(CustomProviderEndpointPolicyTest, HttpsReachesAnything) {
  for (const char *endpoint : {
           "https://api.example.test",
           "https://api.example.test/",
           "https://api.example.test/v1",
           "https://api.example.test:8443/openai/v1",
           "https://192.168.1.9/v1",
           "https://8.8.8.8/v1",
           "https://taffy.local/v1",
       }) {
    EXPECT_TRUE(MayRegister(endpoint)) << endpoint;
  }
}

// Every literal local form, over plain http. Three of these are the same
// address written three ways, which is the whole argument for canonicalizing
// rather than matching.
TEST(CustomProviderEndpointPolicyTest, CleartextReachesALiteralLocalAddress) {
  for (const char *endpoint : {
           "http://127.0.0.1:11434/v1",
           "http://127.4.5.6/v1",
           "http://0x7f.1/v1",
           "http://2130706433/v1",
           "http://[::1]:8080/v1",
           "http://10.0.0.5:8000/v1",
           "http://172.16.4.4/v1",
           "http://192.168.1.9:11434/v1",
           "http://169.254.7.7/v1",
           "http://[fe80::1]/v1",
           "http://[fd00::9]:8000/v1",
           "http://[::ffff:192.168.1.9]/v1",
           "http://[::ffff:127.0.0.1]/v1",
           "http://taffy.local:11434/v1",
           "http://localhost:11434/v1",
           "http://ollama.localhost:11434/v1",
       }) {
    EXPECT_TRUE(MayRegister(endpoint)) << endpoint;
    EXPECT_TRUE(IsLiteralLocalEndpoint(endpoint)) << endpoint;
  }
}

// The refusal the IPv4-mapped acceptances above would become if the ranges
// were consulted before the address was canonicalized: every IPv4-mapped
// literal sits inside the reserved `::/8` block, so a check that asked "is
// this publicly routable" would call a public address local.
TEST(CustomProviderEndpointPolicyTest, AMappedPublicAddressIsStillPublic) {
  EXPECT_FALSE(MayRegister("http://[::ffff:8.8.8.8]/v1"));
  EXPECT_FALSE(IsLiteralLocalEndpoint("http://[::ffff:8.8.8.8]/v1"));
  EXPECT_EQ(ClassifyCustomProviderEndpoint("http://[::ffff:8.8.8.8]/v1"),
            CustomEndpointRefusal::kCleartextNotLocal);
}

// A name is refused over http however it resolves, because resolution happens
// after this check and the answer can change between them. `.local` and
// `localhost` are the two name forms admitted, and both are admitted by their
// shape rather than by a lookup — `localhost` because RFC 6761 reserves it and
// no party owns it to repoint, which is exactly the property every name below
// lacks. `taffy.localdomain` is the near-miss that proves the suffix is
// compared as a label rather than as a substring.
TEST(CustomProviderEndpointPolicyTest, ANameIsRefusedOverCleartext) {
  for (const char *endpoint : {
           "http://models.example.test/v1",
           "http://nowhere.invalid/v1",
           "http://local/v1",
           "http://taffy.localdomain/v1",
           "http://localhost.example.test/v1",
       }) {
    EXPECT_EQ(ClassifyCustomProviderEndpoint(endpoint),
              CustomEndpointRefusal::kCleartextNotLocal)
        << endpoint;
  }
}

// The two instance-metadata addresses sit inside ranges this file otherwise
// admits — the IPv4 one is link-local, the IPv6 one unique-local — and are
// refused anyway. No model server runs on either, so a person loses nothing,
// and the most-attempted request-forgery destination cannot be registered even
// by a person who was talked into typing it. https does not rescue them: the
// address is refused as an address, not as a scheme.
TEST(CustomProviderEndpointPolicyTest, InstanceMetadataIsNotALocalServer) {
  for (const char *endpoint : {
           "http://169.254.169.254/v1",
           "http://[fd00:ec2::254]/v1",
           "http://[::ffff:169.254.169.254]/v1",
       }) {
    EXPECT_EQ(ClassifyCustomProviderEndpoint(endpoint),
              CustomEndpointRefusal::kCleartextNotLocal)
        << endpoint;
    EXPECT_FALSE(IsLiteralLocalEndpoint(endpoint)) << endpoint;
  }
  EXPECT_FALSE(IsLiteralLocalEndpoint("https://169.254.169.254/v1"));
}

TEST(CustomProviderEndpointPolicyTest, APortAndAPathAreAllowed) {
  // The address decision 0096 exists for: a port and a base path, both of
  // which today's origin-only rule throws away.
  EXPECT_TRUE(MayRegister("http://192.168.1.9:11434/v1"));
  EXPECT_TRUE(MayRegister("https://models.example.test:8443/inference/v1/"));
}

TEST(CustomProviderEndpointPolicyTest, CredentialsQueryAndFragmentAreRefused) {
  EXPECT_EQ(ClassifyCustomProviderEndpoint(
                "https://user:password@models.example.test/v1"),
            CustomEndpointRefusal::kCarriesCredentials);
  EXPECT_EQ(
      ClassifyCustomProviderEndpoint("https://models.example.test/v1?k=1"),
      CustomEndpointRefusal::kCarriesQuery);
  EXPECT_EQ(ClassifyCustomProviderEndpoint("https://models.example.test/v1#f"),
            CustomEndpointRefusal::kCarriesFragment);
}

TEST(CustomProviderEndpointPolicyTest, AnythingThatIsNotAnAddressIsRefused) {
  for (const char *endpoint : {
           "",
           "models.example.test",
           "ftp://models.example.test/v1",
           "wss://models.example.test/v1",
           "file:///etc/passwd",
           "data:text/plain,hello",
           "https://",
       }) {
    EXPECT_EQ(ClassifyCustomProviderEndpoint(endpoint),
              CustomEndpointRefusal::kNotAnAddress)
        << endpoint;
  }
}

TEST(CustomProviderEndpointPolicyTest, TheContractsBoundIsTheLengthRefused) {
  const std::string host = "https://models.example.test/";
  const std::string too_long =
      host + std::string(static_cast<size_t>(mojom::kMaxProviderEndpointBytes),
                         'a');
  EXPECT_EQ(ClassifyCustomProviderEndpoint(too_long),
            CustomEndpointRefusal::kTooLong);
}

} // namespace
} // namespace taffy
