// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/test/bip_fixture_manifest.h"

#include <optional>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/values.h"

namespace taffy::test {

namespace {

// Where the fork tooling is expected to mount test-fixtures/web. The overlay
// mounts taffy-core at src/taffy; the corpus
// needs the same treatment, and until it has it every test here fails at Load()
// with this path in the message. That is the correct behaviour: see the header.
constexpr base::FilePath::CharType kCorpusRelativePath[] =
    FILE_PATH_LITERAL("taffy/test/data/web");

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

}  // namespace

BipFixture::BipFixture() = default;
BipFixture::BipFixture(const BipFixture&) = default;
BipFixture::~BipFixture() = default;

BipFixtureManifest::BipFixtureManifest() = default;
BipFixtureManifest::BipFixtureManifest(BipFixtureManifest&&) = default;
BipFixtureManifest::~BipFixtureManifest() = default;

// static
base::FilePath BipFixtureManifest::ManifestPath() {
  base::FilePath source_root;
  // VERIFY AT SP-01: base::DIR_SRC_TEST_DATA_ROOT is the current spelling of
  // what used to be base::DIR_SOURCE_ROOT. Confirm at the pinned milestone;
  // only this line changes.
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  return source_root.Append(kCorpusRelativePath)
      .Append(FILE_PATH_LITERAL("manifest.json"));
}

// static
base::FilePath BipFixtureManifest::OriginRoot(const std::string& origin_key) {
  return ManifestPath()
      .DirName()
      .Append(FILE_PATH_LITERAL("origins"))
      .AppendASCII(origin_key);
}

// static
BipFixtureManifest BipFixtureManifest::Load() {
  const base::FilePath path = ManifestPath();
  std::string json;
  CHECK(base::ReadFileToString(path, &json))
      << "The web fixture corpus is not mounted. Expected " << path
      << ". A lifecycle test that skipped because it could not find the pages "
         "it was supposed to navigate would produce the evidence without the "
         "assurance, so this is a failure rather than a skip.";

  // The nested Value::Dict and Value::List types were lifted out to
  // base::DictValue and base::ListValue at the pinned milestone, and
  // JSONReader::Read no longer defaults its options argument. The corpus
  // manifest is plain RFC JSON: it is generated, and a comment or a trailing
  // comma in it would be a defect in the generator rather than something to
  // tolerate here.
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(parsed && parsed->is_dict()) << "Corpus manifest is not an object: "
                                     << path;
  const base::DictValue& root = parsed->GetDict();

  BipFixtureManifest manifest;
  manifest.root_ = path.DirName();
  const std::string* version = root.FindString("version");
  CHECK(version) << "Corpus manifest has no version. The version is what makes "
                    "a baseline comparison meaningful.";
  manifest.version_ = *version;

  const base::DictValue* origins = root.FindDict("origins");
  CHECK(origins) << "Corpus manifest declares no origins.";
  for (const auto [key, value] : *origins) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    if (const std::string* hostname = entry->FindString("hostname")) {
      manifest.origin_hosts_.emplace_back(key, *hostname);
    }
  }

  const base::ListValue* fixtures = root.FindList("fixtures");
  CHECK(fixtures) << "Corpus manifest declares no fixtures.";
  for (const base::Value& value : *fixtures) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    BipFixture fixture;
    const std::string* id = entry->FindString("id");
    const std::string* url_path = entry->FindString("url_path");
    const std::string* origin = entry->FindString("origin");
    const std::string* family = entry->FindString("family");
    if (!id || !url_path || !origin || !family) {
      continue;
    }
    fixture.id = *id;
    fixture.url_path = *url_path;
    fixture.origin = *origin;
    fixture.family = *family;
    fixture.navigation_transitions =
        ReadStringList(*entry, "navigation_transitions");
    fixture.prohibited_actions = ReadStringList(*entry, "prohibited_actions");
    fixture.verifier_postconditions =
        ReadStringList(*entry, "verifier_postconditions");
    manifest.fixtures_.push_back(std::move(fixture));
  }
  CHECK(!manifest.fixtures_.empty()) << "Corpus manifest listed no fixtures.";
  return manifest;
}

const BipFixture& BipFixtureManifest::ById(const std::string& id) const {
  for (const BipFixture& fixture : fixtures_) {
    if (fixture.id == id) {
      return fixture;
    }
  }
  // NOTREACHED() rather than CHECK(false) plus a return: the pinned milestone
  // marks it [[noreturn]], so the fallback return below it was unreachable and
  // -Wunreachable-code-return rejected it.
  NOTREACHED() << "No fixture named " << id << " in corpus version " << version_
               << ". A test naming a fixture the corpus does not contain is a "
                  "test asserting nothing.";
}

std::string BipFixtureManifest::HostForOrigin(
    const std::string& origin_key) const {
  for (const auto& [key, host] : origin_hosts_) {
    if (key == origin_key) {
      return host;
    }
  }
  NOTREACHED() << "No corpus origin named " << origin_key;
}

}  // namespace taffy::test
