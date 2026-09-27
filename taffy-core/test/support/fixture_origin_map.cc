// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/fixture_origin_map.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/corpus/corpus_mount.h"
#include "taffy/test/corpus/corpus_origin.h"
#include "taffy/test/support/fixture_dynamic_endpoints.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/request_handler_util.h"

namespace taffy::test {

namespace {

// The hostname each corpus origin is served on inside a browser test. It is
// deliberately not the corpus's own, and there is one table rather than one per
// scheme: two constraints have to hold at once, and a per-scheme table is how
// one of them came to hold under the secure scheme and not the plain one.
//
// FOUR DISTINCT SITES. Site isolation, cookie scoping and the frame policy all
// key on scheme plus registrable domain — on site, not on origin. The corpus's
// own four hostnames (primary.taffy.test, partner.taffy.test, embed.taffy.test,
// hostile.taffy.test) are four subdomains of the single registrable domain
// taffy.test, so under --site-per-process all four collapse onto one site and
// share one renderer process. A "cross-origin" frame then runs inside its
// parent's process. Only one assertion in this component notices —
// FramesTest.CrossOriginFramesAreOutOfProcess — while every other frame-scoping
// assertion passes for the wrong reason: they are decided on origin, which does
// differ, so they read as green while measuring a process model the product
// does not have. That vacuity is the thing the out-of-process test exists to
// refuse, and serving the corpus's hostnames is what produced it.
//
// A CERTIFICATE THAT COVERS THEM. Over HTTPS the embedded test server's
// CERT_TEST_NAMES certificate covers a.test, b.test, c.test and d.test, each
// with a single-label wildcard, and nothing else.
//
// One name per corpus role, each under a registrable domain no other role uses
// and each inside a covered wildcard, satisfies both at once. The role name
// stays in the hostname so that a URL in a failure message still says which
// corpus origin it belongs to.
//
// VERIFY AT SP-01: which hostnames the embedded test server's default test
// certificate covers at the pinned milestone. Upstream files to read:
// net/test/embedded_test_server/embedded_test_server.h (SetSSLConfig and the
// ServerCertificate enumeration) and net/data/ssl/certificates/. If the
// certificate stops covering four distinct sites, Start() below CHECK-fails
// with the site it computed rather than quietly collapsing two corpus origins
// onto one.
struct HostAssignment {
  const char* origin_key;
  const char* host;
};

constexpr HostAssignment kHosts[] = {
    {"primary", "primary.a.test"},
    {"partner", "partner.b.test"},
    {"embed", "embed.c.test"},
    {"hostile", "hostile.d.test"},
};

std::string AssignedHostFor(const std::string& origin_key) {
  for (const HostAssignment& assignment : kHosts) {
    if (origin_key == assignment.origin_key) {
      return assignment.host;
    }
  }
  return std::string();
}

// The site a hostname belongs to, by the same rule the browser uses to decide
// which renderer process a document may share. Empty when the host is an IP
// literal or is itself a registry.
std::string SiteOf(const std::string& host) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
      host, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

// The static half of one corpus origin, run after that origin's dynamic
// endpoints have declined the request. Registered once per origin on the one
// server, and — like the dynamic handlers — declines anything addressed to
// another origin's virtual host, so a page that asks the primary origin for a
// partner-only path gets the 404 it deserves rather than the partner's file.
//
// Returning null on a missing file is upstream's behaviour, not a choice made
// here: net::test_server::HandleFileRequest does it, and the embedded test
// server's own 404 is what the request falls through to once every origin's
// handler has declined.
std::unique_ptr<net::test_server::HttpResponse> ServeCorpusFile(
    const std::string& origin_key,
    const std::string& origin_host,
    const net::test_server::HttpRequest& request) {
  if (RequestOriginHost(request) != origin_host) {
    return nullptr;
  }
  return net::test_server::HandleFileRequest(
      CorpusMount::OriginRoot(origin_key), request);
}

}  // namespace

FixtureOriginMap::FixtureOriginMap(Scheme scheme) : scheme_(scheme) {}

FixtureOriginMap::~FixtureOriginMap() = default;

void FixtureOriginMap::Start() {
  CHECK(!started_) << "FixtureOriginMap::Start() called twice.";
  CHECK(CorpusMount::IsMounted()) << CorpusMount::MountInstructions();

  const CorpusManifest& manifest = CorpusManifest::Get();

  for (const CorpusOrigin& origin : manifest.origins()) {
    OriginEntry entry;
    entry.key = origin.key;
    entry.host = AssignedHostFor(origin.key);
    CHECK(!entry.host.empty())
        << "Corpus origin " << origin.key
        << " has no hostname assigned. Add it to kHosts, under a registrable "
           "domain no other corpus origin uses and inside the test "
           "certificate's coverage. Two origins on one site would make the "
           "site-isolation, cookie and frame-policy assertions vacuous, and "
           "the corpus's own hostname is not an answer: all four of those are "
           "subdomains of taffy.test.";
    entry.host = base::ToLowerASCII(entry.host);
    entries_.push_back(std::move(entry));
  }

  CHECK_GE(entries_.size(), 2u)
      << "Fewer than two corpus origins started. Cross-origin behaviour cannot "
         "be observed on one origin, so a suite that ran here would pass "
         "without testing what it claims to test.";

  // With one port for every origin, the hostname is the whole of what makes
  // two corpus origins different. Two origins sharing one would not fail
  // anywhere visible: they would simply be the same origin, and every
  // cross-origin assertion between them would hold for the wrong reason.
  std::set<std::string> hosts;
  for (const OriginEntry& entry : entries_) {
    CHECK(hosts.insert(entry.host).second)
        << "Two corpus origins were assigned the hostname " << entry.host
        << ". Every origin needs its own, because it is the only thing "
           "separating them on a single server.";
  }

  // Distinct hostnames are not enough, and the difference is the whole reason
  // this map does not serve the corpus's own names. Two hostnames under one
  // registrable domain are two origins on ONE site: the browser gives them one
  // renderer process under site isolation and treats requests between them as
  // same-site. Every assertion decided on origin still passes, so nothing else
  // in this component reports it. Checking the site rather than trusting the
  // table above is what turns that from a comment into a failure.
  std::set<std::string> sites;
  for (const OriginEntry& entry : entries_) {
    const std::string site = SiteOf(entry.host);
    CHECK(!site.empty())
        << "Corpus origin " << entry.key << " was assigned the hostname "
        << entry.host
        << ", which has no registrable domain of its own. A bare registry or "
           "an IP literal cannot be one of four separate sites.";
    CHECK(sites.insert(site).second)
        << "Corpus origin " << entry.key << " was assigned the hostname "
        << entry.host << ", whose site " << site
        << " another corpus origin already holds. Two origins on one site "
           "share a renderer process and count as same-site, so every "
           "out-of-process, cookie-scoping and frame-policy assertion between "
           "them would hold for a reason that has nothing to do with the "
           "property under test.";
  }

  server_ = std::make_unique<net::EmbeddedTestServer>(
      scheme_ == Scheme::kHttps ? net::EmbeddedTestServer::TYPE_HTTPS
                                : net::EmbeddedTestServer::TYPE_HTTP);
  if (scheme_ == Scheme::kHttps) {
    server_->SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  }

  const std::string origin_helper = BindSharedOriginHelper();

  // Two passes, and the order between them matters. Every origin's dynamic
  // endpoints are request handlers, which the embedded test server runs before
  // any default handler; the static file handlers are default handlers. So a
  // dynamic endpoint always wins over a file of the same path, on every origin,
  // which is what the /_fixture and /auth rows depend on.
  for (const OriginEntry& entry : entries_) {
    FixtureDynamicEndpoints::RegisterOn(
        server_.get(), entry.key, entry.host, &sentinel_, origin_helper,
        base::BindRepeating(&FixtureOriginMap::ResolveSibling,
                            base::Unretained(this)));
  }
  for (const OriginEntry& entry : entries_) {
    server_->RegisterDefaultHandler(
        base::BindRepeating(&ServeCorpusFile, entry.key, entry.host));
  }

  CHECK(server_->Start())
      << "The corpus server did not start. Every suite in this directory needs "
         "it: without it there is no corpus at all, and a suite that ran here "
         "would report on pages it never loaded.";
  started_ = true;
}

std::string FixtureOriginMap::BindSharedOriginHelper() const {
  // A corpus page never writes an absolute URL. It names a sibling by corpus
  // origin key, and shared/origin-links.js resolves it against a table of the
  // corpus's own hostnames — a table the helper also uses to decide whether it
  // is being served in the corpus's hosts mode or its ports mode.
  //
  // This map does not serve those hostnames, for the reason kHosts explains.
  // Served unchanged, the helper would match none of them, conclude it was in
  // ports mode, and build every sibling URL by subtracting a port offset from
  // the port it was loaded on. Every cross-origin frame, popup and redirect
  // target in the corpus — thirteen pages — would be requested from a port
  // nothing is listening on, or worse from a port that another origin owns. The
  // failure is the quiet one this class's header warns about: the frame still
  // commits, on the opener's origin, and a cross-origin case silently becomes a
  // same-origin one.
  //
  // Binding the table as the helper is served, rather than editing the corpus,
  // is the right side of the seam. Which hostname an origin is served on has
  // always belonged to the deployment — the corpus's own server offers two
  // shapes and only one of them uses those names at all — and the corpus is a
  // versioned contract whose rule is that a change to it is a version bump and
  // never a silent rewrite (test-fixtures/web/README.md). A browser test may
  // not spend that to fix its own hostname policy.
  const base::FilePath path =
      CorpusMount::SharedRoot().AppendASCII("origin-links.js");
  std::string helper;
  CHECK(base::ReadFileToString(path, &helper))
      << "The corpus's shared cross-origin helper is missing at "
      << path.value()
      << ". Every cross-origin fixture resolves its sibling through it, so "
         "without it no frame, popup or redirect target is ever built.";

  for (const CorpusOrigin& origin : CorpusManifest::Get().origins()) {
    const std::string from = base::StrCat({"\"", origin.hostname, "\""});
    const std::string to = base::StrCat({"\"", HostFor(origin.key), "\""});
    const size_t at = helper.find(from);
    CHECK(at != std::string::npos)
        << "The corpus's shared cross-origin helper does not name "
        << origin.hostname << ", so this map cannot bind the " << origin.key
        << " origin to " << HostFor(origin.key)
        << ". Either the helper stopped carrying a hostname table or it and "
           "the manifest disagree; either way a fixture asking for that origin "
           "would be sent somewhere else.";
    CHECK_EQ(helper.find(from, at + from.size()), std::string::npos)
        << "The corpus's shared cross-origin helper names " << origin.hostname
        << " more than once. Rewriting only the first occurrence would leave "
           "the helper half bound, which fails less visibly than not binding "
           "it at all.";
    helper.replace(at, from.size(), to);
  }
  return helper;
}

const FixtureOriginMap::OriginEntry& FixtureOriginMap::EntryFor(
    std::string_view origin_key) const {
  for (const OriginEntry& entry : entries_) {
    if (entry.key == origin_key) {
      return entry;
    }
  }
  // NOTREACHED() rather than CHECK(false) plus a return: NOTREACHED() is
  // [[noreturn]] at the pinned milestone, so the return that used to follow it
  // is dead code the build rejects (-Wunreachable-code-return).
  NOTREACHED() << "No corpus origin named " << origin_key
               << " is served by this map.";
}

GURL FixtureOriginMap::Url(std::string_view origin_key,
                           std::string_view path) const {
  CHECK(started_) << "FixtureOriginMap::Url() before Start().";
  const OriginEntry& entry = EntryFor(origin_key);
  return server_->GetURL(entry.host, std::string(path));
}

GURL FixtureOriginMap::FixtureUrl(std::string_view fixture_id) const {
  const CorpusFixture& fixture = CorpusManifest::Get().ById(fixture_id);
  return Url(fixture.origin, fixture.url_path);
}

url::Origin FixtureOriginMap::OriginOf(std::string_view origin_key) const {
  return url::Origin::Create(Url(origin_key, "/"));
}

std::string FixtureOriginMap::HostFor(std::string_view origin_key) const {
  return EntryFor(origin_key).host;
}

std::vector<std::string> FixtureOriginMap::origin_keys() const {
  std::vector<std::string> keys;
  keys.reserve(entries_.size());
  for (const OriginEntry& entry : entries_) {
    keys.push_back(entry.key);
  }
  return keys;
}

GURL FixtureOriginMap::ResolveSibling(const std::string& origin_key,
                                      const std::string& path) const {
  const OriginEntry& entry = EntryFor(origin_key);
  // Reached from the embedded test server's own sequence while it is answering
  // a request. GetURL only reads the port and the host, both of which are fixed
  // once the server has started, so no lock is needed — but the ordering is:
  // Start() finishes the server before any request can arrive.
  return server_->GetURL(entry.host, path);
}

}  // namespace taffy::test
