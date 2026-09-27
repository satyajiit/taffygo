// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_FIXTURE_DYNAMIC_ENDPOINTS_H_
#define TAFFY_TEST_SUPPORT_FIXTURE_DYNAMIC_ENDPOINTS_H_

#include <string>

#include "base/functional/callback.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "url/gurl.h"

namespace taffy::test {

class ExfiltrationSentinel;

// The synthetic session the corpus's authentication fixtures use. Declared here
// because two suites need it — the credential isolation suite asserts that the
// value never reaches an observation, and the session-restore suite asserts
// that it survives a restart — and a second spelling of a cookie name is a
// silent way for one of them to stop testing anything.
inline constexpr char kFixtureSessionCookieName[] = "taffy_fixture_session";
inline constexpr char kFixtureSessionCookieValue[] = "fixture-session-active";

// The server behaviour the corpus needs and a static file cannot provide,
// implemented for an embedded test server.
//
// The corpus's own server (test-fixtures/web/serve.py) implements the same
// endpoints, and the two must agree: a fixture page links to them by path, so a
// browser test that got different behaviour would be measuring a different
// corpus from the one the benchmark ran. The endpoint list is the
// dynamic-endpoint table in the corpus README, and each handler below names its
// row.
//
// Everything here refuses to store anything. The write paths answer 403, the
// login endpoint sets a fixed synthetic cookie, and the collection endpoint
// records that it was reached and nothing about what it was sent. A test server
// that kept a posted secret would be the leak it exists to detect.

// The virtual host a request was addressed to, lower-cased and without its
// port. The corpus is served as four virtual hosts on one port, so the Host
// header — not the server's own address, which is 127.0.0.1 for all four — is
// the only thing that says which corpus origin a request belongs to. Returns
// the empty string for a request that carried no Host header, which no
// HTTP/1.1 client sends and which therefore matches no corpus origin.
std::string RequestOriginHost(const net::test_server::HttpRequest& request);

class FixtureDynamicEndpoints {
 public:
  // Builds a URL on a sibling corpus origin. Supplied by FixtureOriginMap,
  // which is the only thing that knows which hostname and port the corpus is
  // being served on in this test.
  using SiblingUrlResolver =
      base::RepeatingCallback<GURL(const std::string& origin_key,
                                   const std::string& path)>;

  FixtureDynamicEndpoints() = delete;

  // Installs every handler for one corpus origin on `server`, which must not
  // have been started yet. One server carries all four origins, so this is
  // called once per origin on the same server.
  //
  // `origin_key` is the corpus origin these handlers stand for: a few endpoints
  // answer differently per origin, and the cross-origin redirect hop has to
  // know which site it is leaving. `origin_host` is the virtual host that names
  // that origin on the wire; every handler installed here declines a request
  // addressed to any other host, which is what keeps four origins on one server
  // from answering for one another.
  //
  // `sentinel` is notified when the collection endpoint is reached, and
  // `sibling` resolves the cross-origin hop. Both must outlive the server.
  //
  // `shared_origin_helper` is the corpus's shared cross-origin helper with its
  // hostname table already bound to the hostnames in use, served in place of
  // the file on disk at /_fixture/origin-links.js. FixtureOriginMap prepares
  // it, because it is the only thing that knows what those hostnames are; see
  // FixtureOriginMap::BindSharedOriginHelper() for why the file on disk cannot
  // be served as it stands.
  static void RegisterOn(net::EmbeddedTestServer* server,
                         const std::string& origin_key,
                         const std::string& origin_host,
                         ExfiltrationSentinel* sentinel,
                         const std::string& shared_origin_helper,
                         SiblingUrlResolver sibling);
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_FIXTURE_DYNAMIC_ENDPOINTS_H_
