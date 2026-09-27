// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_FIXTURE_ORIGIN_MAP_H_
#define TAFFY_TEST_SUPPORT_FIXTURE_ORIGIN_MAP_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "taffy/test/support/exfiltration_sentinel.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"
#include "url/origin.h"

// Serves the deterministic and hostile web fixture corpus over its four
// separate web origins, inside one browser test.
//
// Most of what the protocol has to get right is invisible on a single-origin
// page. Frame inclusion policy, out-of-process iframes, cross-origin redirect
// visibility, popup ownership, and the exfiltration sink all need a second and
// a third site that the browser genuinely treats as different — not a different
// path on the same site. A hostname on its own registrable domain per corpus
// origin is what makes that true rather than simulated: each gets its own
// security origin, its own site, its own process under site isolation, and its
// own cookie jar.
//
// THE HOSTNAMES ARE THIS MAP'S OWN, NOT THE CORPUS'S, under both schemes. The
// corpus's four are subdomains of one registrable domain, which makes them one
// site and one renderer process however many origins they are; the table in the
// .cc file says what replaces them and why, and Start() checks the sites it
// computed rather than trusting the table. The corpus's shared cross-origin
// helper carries the corpus's hostnames, so it is bound to this map's
// assignment as it is served — see BindSharedOriginHelper().
//
// ONE SERVER, FOUR VIRTUAL HOSTS, ONE PORT, and that is load bearing rather
// than an economy. This is the corpus's own "hosts mode", the shape
// `test-fixtures/web/serve.py --mode hosts` serves and the one
// `shared/origin-links.js` resolves against: a page builds a sibling origin's
// URL as "same scheme, same port, sibling hostname", because it has no way to
// learn a port it was not loaded from. Four servers would each get an
// arbitrary ephemeral port, so every client-side cross-origin reference in the
// corpus — the popup opener, the OOPIF hosts, the injection frames, thirteen
// pages in all — would be sent to whichever origin happened to own the port
// the page was loaded from, and would 404 there. The failure is quiet in
// exactly the wrong way: the frame or popup still commits, on the *opener's*
// origin, so a cross-origin case silently becomes a same-origin one.
//
// The map is deliberately not a singleton. A browser test that discards its
// server between cases is a browser test whose port and cookie state do not
// leak into the next one, and the corpus is small enough that starting a
// server is not the slow part of anything.

namespace taffy::test {

class FixtureOriginMap {
 public:
  enum class Scheme {
    // The default. Cross-origin behaviour, site isolation, cookie scoping and
    // frame policy are all fully exercised over plain HTTP.
    kHttp,
    // For the rows that are about transport: secure-context gating, mixed
    // content, and the certificate interstitial. Adds Chromium's own test
    // certificate over the same hostnames the plain scheme uses; the host
    // table is chosen to be covered by it, so the two schemes serve the same
    // names and no assertion can hold on one and not the other.
    kHttps,
  };

  explicit FixtureOriginMap(Scheme scheme = Scheme::kHttp);
  FixtureOriginMap(const FixtureOriginMap&) = delete;
  FixtureOriginMap& operator=(const FixtureOriginMap&) = delete;
  ~FixtureOriginMap();

  // Starts the corpus server, carrying every corpus origin as a virtual host.
  // The caller must already have pointed the host resolver at the loopback
  // interface; the map cannot do it, because the resolver rule belongs to the
  // browser test fixture and outlives this object.
  //
  // CHECK-fails when the corpus is not mounted or the server cannot start.
  // Both are failures rather than skips, for the reason CorpusMount explains.
  void Start();

  // True once Start() has run and the server is accepting connections.
  bool started() const { return started_; }

  // A URL on one corpus origin. `path` is a served path, for example
  // "/frames/nested-oopif.html".
  GURL Url(std::string_view origin_key, std::string_view path) const;

  // The URL of a fixture named by its corpus identifier. This is the form
  // almost every test should use: it takes the origin and the path from the
  // manifest, so a corpus that moves a page moves the test with it.
  GURL FixtureUrl(std::string_view fixture_id) const;

  url::Origin OriginOf(std::string_view origin_key) const;

  // The hostname this map assigned to a corpus origin. It is not the hostname
  // the corpus's own server uses, under either scheme; the .cc file's host
  // table says why, and this is the only place that knows.
  std::string HostFor(std::string_view origin_key) const;

  // Every corpus origin key, in manifest order.
  std::vector<std::string> origin_keys() const;

  // The one server every corpus origin is served from. There is no per-origin
  // server to ask for: the origins are virtual hosts on this one, which is
  // what the class comment above explains.
  net::EmbeddedTestServer* server() { return server_.get(); }

  // Records every request that reached the corpus's collection endpoint. A
  // correct run produces none; TaffyBrowserTestBase asserts that after every
  // test so that no individual case has to remember.
  const ExfiltrationSentinel& sentinel() const { return sentinel_; }

 private:
  struct OriginEntry {
    std::string key;
    std::string host;
  };

  const OriginEntry& EntryFor(std::string_view origin_key) const;

  // The corpus's shared cross-origin helper, with its hostname table bound to
  // the hostnames this map assigned. Read once from the mounted corpus at
  // Start() and handed to every origin's dynamic endpoints; the definition
  // explains why the binding happens here rather than in the corpus.
  std::string BindSharedOriginHelper() const;

  // Resolves a sibling origin's URL for the cross-origin redirect hop. Bound
  // into the dynamic endpoints, which cannot know the server's address
  // themselves.
  GURL ResolveSibling(const std::string& origin_key,
                      const std::string& path) const;

  const Scheme scheme_;
  bool started_ = false;
  ExfiltrationSentinel sentinel_;
  std::vector<OriginEntry> entries_;
  std::unique_ptr<net::EmbeddedTestServer> server_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_FIXTURE_ORIGIN_MAP_H_
