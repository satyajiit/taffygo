// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/corpus/corpus_manifest.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/notreached.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "taffy/test/corpus/corpus_mount.h"

namespace taffy::test {

namespace {

std::vector<std::string> ReadStringList(const base::DictValue& dict,
                                        const char* key) {
  std::vector<std::string> out;
  const base::ListValue* list = dict.FindList(key);
  if (!list) {
    return out;
  }
  for (const base::Value& value : *list) {
    if (const std::string* text = value.GetIfString()) {
      out.push_back(*text);
    }
  }
  return out;
}

std::string ReadString(const base::DictValue& dict, const char* key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

std::vector<CorpusExpectedField> ReadExpectedFields(
    const base::DictValue& dict) {
  std::vector<CorpusExpectedField> out;
  const base::ListValue* list = dict.FindList("expected_semantic_fields");
  if (!list) {
    return out;
  }
  for (const base::Value& value : *list) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    CorpusExpectedField field;
    field.field = ReadString(*entry, "field");
    field.value = ReadString(*entry, "value");
    field.source = ReadString(*entry, "source");
    out.push_back(std::move(field));
  }
  return out;
}

}  // namespace

CorpusManifest::CorpusManifest() = default;
CorpusManifest::~CorpusManifest() = default;

// static
const CorpusManifest& CorpusManifest::Get() {
  static base::NoDestructor<CorpusManifest> instance;
  static bool loaded = false;
  if (!loaded) {
    // Set before Load() so that a CHECK inside Load() cannot be re-entered by a
    // second caller during stack unwinding.
    loaded = true;
    instance->Load();
  }
  return *instance;
}

void CorpusManifest::Load() {
  // Read from disk, and a browser test's body runs on the UI thread where
  // blocking is disallowed, so without this the load aborts the process
  // instead of failing the test that asked for a fixture.
  base::ScopedAllowBlockingForTesting allow_blocking;
  const base::FilePath path = CorpusMount::ManifestPath();
  std::string json;
  CHECK(base::ReadFileToString(path, &json)) << CorpusMount::MountInstructions();

  // The options argument is required at the pinned milestone. JSON_PARSE_RFC
  // rather than Chromium's extended dialect: the manifest is written by
  // test-fixtures/web/check.py and read by tools in three languages, so it is
  // strict JSON and a comment or a trailing comma in it is a defect to fail on
  // rather than a convenience to accept.
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(parsed && parsed->is_dict())
      << "The corpus manifest at " << path << " is not a JSON object. The "
      << "corpus self-check (python3 test-fixtures/web/check.py) would have "
      << "caught this before it was committed; run it.";
  const base::DictValue& root = parsed->GetDict();

  corpus_name_ = ReadString(root, "corpus");
  version_ = ReadString(root, "version");
  CHECK(!version_.empty())
      << "The corpus manifest carries no version. The version is what makes a "
         "like-for-like benchmark comparison mean anything, so a corpus "
         "without one cannot be run against.";

  const base::DictValue* sink = root.FindDict("exfiltration_sink");
  CHECK(sink) << "The corpus declares no exfiltration sink. The adversarial "
                 "suite asserts that no request ever reaches it, and it cannot "
                 "assert that against an endpoint the corpus has not named.";
  exfiltration_sink_origin_ = ReadString(*sink, "origin");
  exfiltration_sink_path_ = ReadString(*sink, "url_path");
  CHECK(!exfiltration_sink_origin_.empty() && !exfiltration_sink_path_.empty())
      << "The corpus exfiltration sink has no origin or no path.";

  const base::DictValue* origins = root.FindDict("origins");
  CHECK(origins) << "The corpus manifest declares no origins.";
  for (const auto [key, value] : *origins) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    CorpusOrigin origin;
    origin.key = key;
    origin.hostname = ReadString(*entry, "hostname");
    origin.role = ReadString(*entry, "role");
    origin.port_offset =
        static_cast<uint32_t>(entry->FindInt("port_offset").value_or(0));
    CHECK(!origin.hostname.empty())
        << "Corpus origin " << key << " declares no hostname.";
    origins_.push_back(std::move(origin));
  }
  CHECK_GE(origins_.size(), 2u)
      << "The corpus declares fewer than two origins. Frame inclusion policy, "
         "out-of-process iframes, cross-origin redirects and popup ownership "
         "are all invisible on a single-origin corpus.";

  const base::ListValue* canaries = root.FindList("canaries");
  CHECK(canaries) << "The corpus manifest declares no canaries. Zero "
                     "seeded-secret leakage cannot be measured without seeded "
                     "secrets.";
  for (const base::Value& value : *canaries) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    CorpusCanary canary;
    canary.token = ReadString(*entry, "token");
    canary.secret_class = ReadString(*entry, "class");
    canary.carried_by = ReadStringList(*entry, "carried_by");
    CHECK(!canary.token.empty()) << "A corpus canary has no token.";
    canaries_.push_back(std::move(canary));
  }
  CHECK(!canaries_.empty()) << "The corpus declares an empty canary list.";

  const base::ListValue* fixtures = root.FindList("fixtures");
  CHECK(fixtures) << "The corpus manifest declares no fixtures.";
  for (const base::Value& value : *fixtures) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    CorpusFixture fixture;
    fixture.id = ReadString(*entry, "id");
    fixture.path = ReadString(*entry, "path");
    fixture.url_path = ReadString(*entry, "url_path");
    fixture.origin = ReadString(*entry, "origin");
    fixture.family = ReadString(*entry, "family");
    fixture.title = ReadString(*entry, "title");
    CHECK(!fixture.id.empty() && !fixture.path.empty() &&
          !fixture.url_path.empty() && !fixture.origin.empty() &&
          !fixture.family.empty())
        << "A corpus fixture is missing one of id, path, url_path, origin or "
           "family. The corpus self-check enforces all five; run "
           "python3 test-fixtures/web/check.py.";
    fixture.expected_semantic_fields = ReadExpectedFields(*entry);
    fixture.sensitive_omissions = ReadStringList(*entry, "sensitive_omissions");
    fixture.allowed_actions = ReadStringList(*entry, "allowed_actions");
    fixture.prohibited_actions = ReadStringList(*entry, "prohibited_actions");
    fixture.navigation_transitions =
        ReadStringList(*entry, "navigation_transitions");
    fixture.verifier_postconditions =
        ReadStringList(*entry, "verifier_postconditions");
    fixtures_.push_back(std::move(fixture));
  }
  CHECK(!fixtures_.empty()) << "The corpus manifest listed no fixtures.";

  // Every fixture names an origin the corpus also declares. A fixture on an
  // origin with no server is a page no suite can reach, and the failure it
  // produces later is a connection error rather than an explanation.
  for (const CorpusFixture& fixture : fixtures_) {
    bool known = false;
    for (const CorpusOrigin& origin : origins_) {
      if (origin.key == fixture.origin) {
        known = true;
        break;
      }
    }
    CHECK(known) << "Fixture " << fixture.id << " names origin "
                 << fixture.origin << ", which the corpus does not declare.";
  }

  // Every canary names a fixture that exists, for the same reason.
  for (const CorpusCanary& canary : canaries_) {
    for (const std::string& fixture_id : canary.carried_by) {
      bool known = false;
      for (const CorpusFixture& fixture : fixtures_) {
        if (fixture.id == fixture_id) {
          known = true;
          break;
        }
      }
      CHECK(known) << "Canary " << canary.token << " is declared for fixture "
                   << fixture_id << ", which the corpus does not contain.";
    }
  }
}

const CorpusFixture& CorpusManifest::ById(std::string_view id) const {
  for (const CorpusFixture& fixture : fixtures_) {
    if (fixture.id == id) {
      return fixture;
    }
  }
  // NOTREACHED() rather than CHECK(false) plus a return: NOTREACHED() is
  // [[noreturn]] at the pinned milestone, so the return that used to follow it
  // is dead code the build rejects (-Wunreachable-code-return).
  NOTREACHED() << "No fixture named " << id << " in corpus " << corpus_name_
               << " version " << version_
               << ". A test naming a fixture the corpus does not contain is a "
                  "test asserting nothing, so this is a failure rather than a "
                  "skip.";
}

const CorpusOrigin& CorpusManifest::OriginByKey(std::string_view key) const {
  for (const CorpusOrigin& origin : origins_) {
    if (origin.key == key) {
      return origin;
    }
  }
  NOTREACHED() << "No corpus origin named " << key << " in corpus "
               << corpus_name_ << " version " << version_ << ".";
}

std::vector<const CorpusFixture*> CorpusManifest::ByFamily(
    std::string_view family) const {
  std::vector<const CorpusFixture*> out;
  for (const CorpusFixture& fixture : fixtures_) {
    if (fixture.family == family) {
      out.push_back(&fixture);
    }
  }
  CHECK(!out.empty()) << "No fixture in family " << family
                      << ". A suite iterating an empty family passes for the "
                         "wrong reason.";
  return out;
}

std::vector<std::string> CorpusManifest::AllCanaryTokens() const {
  std::vector<std::string> out;
  out.reserve(canaries_.size());
  for (const CorpusCanary& canary : canaries_) {
    out.push_back(canary.token);
  }
  return out;
}

std::vector<std::string> CorpusManifest::ProhibitedValuesFor(
    std::string_view fixture_id) const {
  const CorpusFixture& fixture = ById(fixture_id);
  std::vector<std::string> out = fixture.sensitive_omissions;
  for (const CorpusCanary& canary : canaries_) {
    for (const std::string& carrier : canary.carried_by) {
      if (carrier == fixture.id) {
        out.push_back(canary.token);
        break;
      }
    }
  }
  return out;
}

std::string CorpusManifest::ReadFixtureHtml(
    std::string_view fixture_id) const {
  const CorpusFixture& fixture = ById(fixture_id);
  base::ScopedAllowBlockingForTesting allow_blocking;
  const base::FilePath path =
      CorpusMount::Root().AppendASCII(fixture.path);
  std::string contents;
  CHECK(base::ReadFileToString(path, &contents))
      << "Fixture " << fixture.id << " is declared at " << fixture.path
      << " but could not be read from " << path << ". "
      << CorpusMount::MountInstructions();
  return contents;
}

}  // namespace taffy::test
