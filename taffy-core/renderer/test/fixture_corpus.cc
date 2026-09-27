// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/test/fixture_corpus.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/notreached.h"
#include "base/strings/string_split.h"
#include "base/values.h"
#include "taffy/test/corpus/corpus_mount.h"

// CorpusMount is the shared authority for the canonical fixture location.
// Keeping the renderer reader on that authority prevents its device path from
// drifting back to the retired duplicate component mount.

namespace taffy::test {

namespace {

std::vector<std::string> StringList(const base::DictValue& dict,
                                    const char* key) {
  std::vector<std::string> out;
  const base::ListValue* list = dict.FindList(key);
  if (!list) {
    return out;
  }
  for (const base::Value& value : *list) {
    if (value.is_string()) {
      out.push_back(value.GetString());
    }
  }
  return out;
}

std::string String(const base::DictValue& dict, const char* key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

}  // namespace

Canary::Canary() = default;
Canary::Canary(const Canary&) = default;
Canary::~Canary() = default;

Fixture::Fixture() = default;
Fixture::Fixture(const Fixture&) = default;
Fixture::~Fixture() = default;

FixtureCorpus::FixtureCorpus() = default;
FixtureCorpus::FixtureCorpus(FixtureCorpus&&) = default;
FixtureCorpus::~FixtureCorpus() = default;

// static
base::FilePath FixtureCorpus::ManifestPath() {
  return CorpusMount::ManifestPath();
}

// static
FixtureCorpus FixtureCorpus::Load() {
  const base::FilePath manifest_path = ManifestPath();
  std::string raw;
  CHECK(base::ReadFileToString(manifest_path, &raw))
      << "the fixture corpus manifest is not at " << manifest_path
      << ". A leak test that cannot find the secrets it is supposed to look "
         "for must fail, not skip: skipping produces the evidence without "
         "the assurance.";

  std::optional<base::Value> parsed =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  CHECK(parsed.has_value() && parsed->is_dict())
      << "the fixture corpus manifest at " << manifest_path
      << " is not a JSON object";
  const base::DictValue& document = parsed->GetDict();

  FixtureCorpus corpus;
  corpus.root_ = manifest_path.DirName();
  corpus.version_ = String(document, "version");
  CHECK(!corpus.version_.empty())
      << "the fixture corpus manifest states no version. The corpus is "
         "immutable and versioned; an unversioned one cannot be compared "
         "like for like against a previous run.";

  if (const base::ListValue* canaries = document.FindList("canaries")) {
    for (const base::Value& value : *canaries) {
      if (!value.is_dict()) {
        continue;
      }
      const base::DictValue& entry = value.GetDict();
      Canary canary;
      canary.token = String(entry, "token");
      canary.secret_class = String(entry, "class");
      canary.carried_by = StringList(entry, "carried_by");
      CHECK(!canary.token.empty()) << "a canary with no token";
      corpus.canaries_.push_back(std::move(canary));
    }
  }
  CHECK(!corpus.canaries_.empty())
      << "the fixture corpus declares no canaries. The zero-leak property "
         "cannot be asserted against a corpus with no seeded secrets in it.";

  if (const base::ListValue* fixtures = document.FindList("fixtures")) {
    for (const base::Value& value : *fixtures) {
      if (!value.is_dict()) {
        continue;
      }
      const base::DictValue& entry = value.GetDict();
      Fixture fixture;
      fixture.id = String(entry, "id");
      fixture.path = String(entry, "path");
      fixture.url_path = String(entry, "url_path");
      fixture.origin = String(entry, "origin");
      fixture.family = String(entry, "family");
      fixture.sensitive_omissions = StringList(entry, "sensitive_omissions");
      CHECK(!fixture.id.empty()) << "a fixture with no id";
      corpus.fixtures_.push_back(std::move(fixture));
    }
  }
  CHECK(!corpus.fixtures_.empty()) << "the fixture corpus declares no pages";
  return corpus;
}

const Fixture& FixtureCorpus::ById(const std::string& id) const {
  for (const Fixture& fixture : fixtures_) {
    if (fixture.id == id) {
      return fixture;
    }
  }
  // VERIFY AT SP-04: base::NotReached() is [[noreturn]] at the pin. If it is
  // not, this needs a trailing return of a reference that cannot be formed -
  // in which case the lookup returns std::optional instead and every caller
  // asserts.
  NOTREACHED() << "no fixture named '" << id
               << "' in corpus version " << version_
               << ". A test naming a fixture the corpus does not contain "
                  "asserts nothing.";
}

std::vector<std::string> FixtureCorpus::AllCanaryTokens() const {
  std::vector<std::string> tokens;
  tokens.reserve(canaries_.size());
  for (const Canary& canary : canaries_) {
    tokens.push_back(canary.token);
  }
  return tokens;
}

std::string FixtureCorpus::ReadFixtureHtml(const std::string& id) const {
  const Fixture& fixture = ById(id);
  base::FilePath path = root_;
  for (const std::string& part :
       base::SplitString(fixture.path, "/", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    path = path.AppendASCII(part);
  }
  std::string html;
  CHECK(base::ReadFileToString(path, &html))
      << "fixture '" << id << "' is not at " << path;
  return html;
}

}  // namespace taffy::test
