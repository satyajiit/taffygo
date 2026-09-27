// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/values.h"
#include "mojo/core/embedder/embedder.h"
#include "taffy/test/fuzz/wire_seed_builder.h"

// Writes the fuzzer seed corpus for every browser-facing renderer payload, from
// the contract's own golden documents and compatibility fixtures.
//
// Run by a GN action at build time; see fuzz/BUILD.gn. It is a host tool rather
// than a script because the seeds have to be Mojo wire bytes, and the only
// thing that can produce those is code linked against the generated bindings.
//
// **It fails rather than skipping.** A golden document whose definition has no
// builder and is not on the list of messages that never cross this boundary
// stops the build with the definition named. A seed corpus that quietly stopped
// covering a message would leave a fuzzer starting from nothing, which looks
// exactly like a fuzzer that found nothing.
//
// Usage:
//   write_wire_seeds --golden-dir=DIR --compat-dir=DIR --output-dir=DIR
//                    [--depfile=FILE]

namespace {

struct Options {
  base::FilePath golden_dir;
  base::FilePath compat_dir;
  base::FilePath output_dir;
  base::FilePath depfile;
};

bool ReadJson(const base::FilePath& path, base::DictValue* out) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    std::cerr << "cannot read " << path.AsUTF8Unsafe() << "\n";
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    std::cerr << "not a JSON object: " << path.AsUTF8Unsafe() << "\n";
    return false;
  }
  *out = std::move(*parsed).TakeDict();
  return true;
}

bool WriteSeed(const base::FilePath& output_dir,
               const std::string& name,
               const std::vector<uint8_t>& bytes) {
  const base::FilePath path = output_dir.AppendASCII(name + ".bin");
  return base::WriteFile(path, bytes);
}

// The golden index says which schema definition each document is an instance
// of. Reading it rather than inferring from the filename is what keeps the
// seeds and the contract in step.
bool ProcessGolden(const Options& options,
                   std::vector<std::string>* inputs,
                   int* written) {
  const base::FilePath index_path =
      options.golden_dir.AppendASCII("index.json");
  base::DictValue index;
  if (!ReadJson(index_path, &index)) {
    return false;
  }
  inputs->push_back(index_path.AsUTF8Unsafe());

  const base::ListValue* messages = index.FindList("messages");
  if (!messages) {
    std::cerr << "golden index declares no messages\n";
    return false;
  }

  for (const base::Value& value : *messages) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    const std::string* file = entry->FindString("file");
    const std::string* definition = entry->FindString("definition");
    if (!file || !definition) {
      std::cerr << "golden index entry has no file or definition\n";
      return false;
    }
    if (!taffy::test::WireSeedBuilder::IsBrowserFacingRendererPayload(
            *definition)) {
      // Browser-authored. It never arrives from a renderer, so it is not an
      // input on this boundary and needs no seed here.
      continue;
    }

    const base::FilePath document_path = options.golden_dir.AppendASCII(*file);
    base::DictValue document;
    if (!ReadJson(document_path, &document)) {
      return false;
    }
    inputs->push_back(document_path.AsUTF8Unsafe());

    std::optional<std::vector<uint8_t>> bytes =
        taffy::test::WireSeedBuilder::Build(*definition, document);
    if (!bytes) {
      std::cerr << "no seed builder for definition " << *definition
                << " (golden document " << *file
                << "). Add one to wire_seed_builder.cc, or add the definition "
                   "to the list of messages that never cross this boundary. "
                   "Leaving it out would silently stop seeding a fuzzer.\n";
      return false;
    }
    if (!WriteSeed(options.output_dir, "golden_" + *definition + "_" + *file,
                   *bytes)) {
      std::cerr << "cannot write seed for " << *file << "\n";
      return false;
    }
    ++*written;
  }
  return true;
}

// The compatibility fixtures are each one deliberate deviation from a message
// that would otherwise be accepted. They make unusually good seeds: every one
// sits on a boundary the decoder has to get right.
bool ProcessCompat(const Options& options,
                   std::vector<std::string>* inputs,
                   int* written) {
  const base::FilePath manifest_path =
      options.compat_dir.AppendASCII("manifest.json");
  base::DictValue manifest;
  if (!ReadJson(manifest_path, &manifest)) {
    return false;
  }
  inputs->push_back(manifest_path.AsUTF8Unsafe());

  const base::ListValue* fixtures = manifest.FindList("fixtures");
  if (!fixtures) {
    std::cerr << "compatibility manifest declares no fixtures\n";
    return false;
  }

  for (const base::Value& value : *fixtures) {
    const base::DictValue* entry = value.GetIfDict();
    if (!entry) {
      continue;
    }
    const std::string* file = entry->FindString("file");
    const std::string* definition = entry->FindString("definition");
    if (!file || !definition) {
      std::cerr << "compatibility fixture has no file or definition\n";
      return false;
    }
    if (!taffy::test::WireSeedBuilder::IsBrowserFacingRendererPayload(
            *definition)) {
      continue;
    }

    const base::FilePath document_path = options.compat_dir.AppendASCII(*file);
    base::DictValue document;
    if (!ReadJson(document_path, &document)) {
      // A fixture that is deliberately not well-formed JSON cannot become a
      // structured seed. Its bytes are still a seed for the JSON-facing
      // targets, which read the file directly, so it is copied rather than
      // built.
      std::string raw;
      if (!base::ReadFileToString(document_path, &raw)) {
        return false;
      }
      inputs->push_back(document_path.AsUTF8Unsafe());
      const std::vector<uint8_t> bytes(raw.begin(), raw.end());
      if (!WriteSeed(options.output_dir, "compat_raw_" + *file, bytes)) {
        return false;
      }
      ++*written;
      continue;
    }
    inputs->push_back(document_path.AsUTF8Unsafe());

    std::optional<std::vector<uint8_t>> bytes =
        taffy::test::WireSeedBuilder::Build(*definition, document);
    if (!bytes) {
      std::cerr << "no seed builder for compatibility definition "
                << *definition << "\n";
      return false;
    }
    if (!WriteSeed(options.output_dir, "compat_" + *definition + "_" + *file,
                   *bytes)) {
      return false;
    }
    ++*written;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  mojo::core::Init();
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  Options options;
  options.golden_dir = command_line.GetSwitchValuePath("golden-dir");
  options.compat_dir = command_line.GetSwitchValuePath("compat-dir");
  options.output_dir = command_line.GetSwitchValuePath("output-dir");
  options.depfile = command_line.GetSwitchValuePath("depfile");

  if (options.golden_dir.empty() || options.compat_dir.empty() ||
      options.output_dir.empty()) {
    std::cerr << "usage: write_wire_seeds --golden-dir=DIR --compat-dir=DIR "
                 "--output-dir=DIR [--depfile=FILE]\n";
    return 1;
  }
  if (!base::CreateDirectory(options.output_dir)) {
    std::cerr << "cannot create " << options.output_dir.AsUTF8Unsafe() << "\n";
    return 1;
  }

  std::vector<std::string> inputs;
  int written = 0;
  if (!ProcessGolden(options, &inputs, &written)) {
    return 1;
  }
  if (!ProcessCompat(options, &inputs, &written)) {
    return 1;
  }
  if (written == 0) {
    std::cerr << "no seeds were written. A fuzzer with an empty seed corpus "
                 "starts from nothing, which is indistinguishable from a "
                 "fuzzer that found nothing.\n";
    return 1;
  }

  if (!options.depfile.empty()) {
    // The contract lives outside the Chromium source root, so the inputs are
    // absolute. Declaring them is what makes a contract change rebuild the
    // seeds.
    std::string depfile = base::StrCat(
        {options.output_dir.AppendASCII("seed.stamp").AsUTF8Unsafe(), ":"});
    for (const std::string& input : inputs) {
      depfile = base::StrCat({depfile, " ", input});
    }
    depfile += "\n";
    if (!base::WriteFile(options.depfile, depfile)) {
      std::cerr << "cannot write depfile\n";
      return 1;
    }
  }
  if (!base::WriteFile(options.output_dir.AppendASCII("seed.stamp"),
                       std::string("ok\n"))) {
    return 1;
  }

  std::cout << "wrote " << written << " seed message(s)\n";
  return 0;
}
