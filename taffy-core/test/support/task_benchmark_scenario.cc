// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/task_benchmark_scenario.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/threading/thread_restrictions.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "taffy/test/corpus/corpus_manifest.h"

namespace taffy::test {
namespace {

constexpr base::FilePath::CharType kTaskCorpusRelativePath[] =
    FILE_PATH_LITERAL("taffy/test/data/tasks");

base::FilePath TaskCorpusRoot() {
  base::FilePath root;
  if (!base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root)) {
    return base::FilePath();
  }
  return root.Append(kTaskCorpusRelativePath);
}

std::optional<base::DictValue> ReadObject(const base::FilePath& path,
                                          std::string* error) {
  // The corpus is read from disk, and a browser test runs its body on the UI
  // thread, where blocking is disallowed — so every scenario test aborted here
  // rather than failing. This is the one place the corpus is read, so the
  // scope belongs here rather than at each of its callers.
  base::ScopedAllowBlockingForTesting allow_blocking;
  std::string json;
  if (!base::ReadFileToString(path, &json)) {
    *error = "cannot read Task Benchmark input " + path.AsUTF8Unsafe();
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    *error = "Task Benchmark input is not a strict JSON object: " +
             path.AsUTF8Unsafe();
    return std::nullopt;
  }
  return std::move(*parsed).TakeDict();
}

std::optional<std::string> RequiredString(const base::DictValue& value,
                                          std::string_view key,
                                          std::string_view owner,
                                          std::string* error) {
  const std::string* found = value.FindString(key);
  if (!found || found->empty()) {
    *error = std::string(owner) + " has no non-empty " + std::string(key);
    return std::nullopt;
  }
  return *found;
}

std::optional<std::vector<std::string>> StringList(const base::DictValue& value,
                                                   std::string_view key,
                                                   std::string_view owner,
                                                   std::string* error) {
  const base::ListValue* list = value.FindList(key);
  if (!list) {
    *error = std::string(owner) + " has no " + std::string(key) + " list";
    return std::nullopt;
  }
  std::vector<std::string> result;
  result.reserve(list->size());
  for (const base::Value& item : *list) {
    const std::string* text = item.GetIfString();
    if (!text || text->empty()) {
      *error = std::string(owner) + " has a non-string or empty " +
               std::string(key) + " member";
      return std::nullopt;
    }
    result.push_back(*text);
  }
  return result;
}

std::optional<std::string> OptionalString(const base::DictValue& value,
                                          std::string_view key,
                                          bool* valid) {
  const base::Value* found = value.Find(key);
  if (!found || found->is_none()) {
    return std::nullopt;
  }
  const std::string* text = found->GetIfString();
  if (!text || text->empty()) {
    *valid = false;
    return std::nullopt;
  }
  return *text;
}

bool ReadFacts(const base::DictValue& manifest_row,
               std::string_view scenario_id,
               std::vector<TaskBenchmarkScenario::RequiredFact>* facts_out,
               std::vector<std::string>* forbidden_out,
               std::string* error) {
  const base::ListValue* facts = manifest_row.FindList("required_facts");
  const base::ListValue* forbidden = manifest_row.FindList("forbidden_facts");
  if (!facts || !forbidden) {
    *error = std::string(scenario_id) +
             " has no required_facts or forbidden_facts list";
    return false;
  }

  std::set<std::string> ids;
  for (const base::Value& value : *facts) {
    const base::DictValue* row = value.GetIfDict();
    if (!row) {
      *error = std::string(scenario_id) + " has a non-object required fact";
      return false;
    }
    TaskBenchmarkScenario::RequiredFact fact;
    const auto id = RequiredString(*row, "id", scenario_id, error);
    const auto evidence = RequiredString(*row, "evidence", scenario_id, error);
    const auto tolerance =
        RequiredString(*row, "tolerance", scenario_id, error);
    bool valid_optional = true;
    fact.source_fixture =
        OptionalString(*row, "source_fixture", &valid_optional);
    fact.source_field = OptionalString(*row, "source_field", &valid_optional);
    if (!id || !evidence || !tolerance || !valid_optional ||
        !ids.insert(*id).second) {
      if (error->empty()) {
        *error = std::string(scenario_id) +
                 " has malformed or duplicate required facts";
      }
      return false;
    }
    fact.id = *id;
    fact.evidence = *evidence;
    fact.tolerance = *tolerance;
    if (fact.evidence == "page-field" &&
        (!fact.source_fixture || !fact.source_field)) {
      *error = fact.id + " is page-field evidence without a fixture and field";
      return false;
    }
    facts_out->push_back(std::move(fact));
  }

  ids.clear();
  for (const base::Value& value : *forbidden) {
    const base::DictValue* row = value.GetIfDict();
    const std::string* id = row ? row->FindString("id") : nullptr;
    if (!id || id->empty() || !ids.insert(*id).second) {
      *error = std::string(scenario_id) +
               " has malformed or duplicate forbidden facts";
      return false;
    }
    forbidden_out->push_back(*id);
  }
  return true;
}

bool ReadSteps(const base::DictValue& execution,
               std::string_view scenario_id,
               std::vector<TaskBenchmarkScenario::Step>* steps_out,
               std::vector<TaskBenchmarkScenario::InjectedEvent>* events_out,
               std::string* error) {
  const base::ListValue* steps = execution.FindList("steps");
  if (!steps || steps->empty()) {
    *error = std::string(scenario_id) + " has no execution steps";
    return false;
  }
  for (size_t index = 0u; index < steps->size(); ++index) {
    const base::DictValue* row = (*steps)[index].GetIfDict();
    const std::optional<int> number = row ? row->FindInt("n") : std::nullopt;
    const std::string* kind = row ? row->FindString("kind") : nullptr;
    if (!row || !number || *number != static_cast<int>(index + 1u) || !kind ||
        kind->empty()) {
      *error =
          std::string(scenario_id) + " has a malformed or out-of-order step";
      return false;
    }
    TaskBenchmarkScenario::Step step;
    step.number = static_cast<uint32_t>(*number);
    step.kind = *kind;
    bool valid_optional = true;
    step.fixture = OptionalString(*row, "fixture", &valid_optional);
    const auto produces = StringList(*row, "produces", scenario_id, error);
    if (!valid_optional || !produces) {
      return false;
    }
    step.produces = *produces;
    steps_out->push_back(std::move(step));
  }

  const base::ListValue* events = execution.FindList("injected_events");
  if (!events) {
    *error = std::string(scenario_id) + " has no injected_events list";
    return false;
  }
  for (const base::Value& value : *events) {
    const base::DictValue* row = value.GetIfDict();
    const std::optional<int> before =
        row ? row->FindInt("before_step") : std::nullopt;
    const std::string* event = row ? row->FindString("event") : nullptr;
    if (!before || *before <= 0 ||
        static_cast<size_t>(*before) > steps_out->size() || !event ||
        event->empty()) {
      *error = std::string(scenario_id) + " has a malformed injected event";
      return false;
    }
    events_out->push_back({static_cast<uint32_t>(*before), *event});
  }
  return true;
}

std::string CanonicalDigest(const base::DictValue& manifest_row,
                            const base::DictValue& execution,
                            std::string* error) {
  base::DictValue joined;
  joined.Set("execution", execution.Clone());
  joined.Set("manifest", manifest_row.Clone());
  std::string canonical;
  if (!base::JSONWriter::Write(joined, &canonical)) {
    *error = "could not canonically serialize the joined benchmark scenario";
    return std::string();
  }
  return base::ToLowerASCII(
      base::HexEncode(crypto::SHA256HashString(canonical)));
}

}  // namespace

TaskBenchmarkScenario::TaskBenchmarkScenario() = default;
TaskBenchmarkScenario::TaskBenchmarkScenario(const TaskBenchmarkScenario&) =
    default;
TaskBenchmarkScenario::TaskBenchmarkScenario(TaskBenchmarkScenario&&) noexcept =
    default;
TaskBenchmarkScenario& TaskBenchmarkScenario::operator=(
    const TaskBenchmarkScenario&) = default;
TaskBenchmarkScenario& TaskBenchmarkScenario::operator=(
    TaskBenchmarkScenario&&) noexcept = default;
TaskBenchmarkScenario::~TaskBenchmarkScenario() = default;

// static
std::optional<TaskBenchmarkScenario> TaskBenchmarkScenario::Load(
    std::string_view scenario_id,
    std::string* error) {
  if (!error) {
    return std::nullopt;
  }
  error->clear();
  if (scenario_id.empty() || scenario_id.find('/') != std::string_view::npos ||
      scenario_id.find('\\') != std::string_view::npos) {
    *error = "invalid Task Benchmark scenario id";
    return std::nullopt;
  }

  const base::FilePath root = TaskCorpusRoot();
  if (root.empty()) {
    *error = "no Chromium test-data root is available";
    return std::nullopt;
  }
  std::optional<base::DictValue> manifest =
      ReadObject(root.Append(FILE_PATH_LITERAL("manifest.json")), error);
  if (!manifest) {
    return std::nullopt;
  }
  const auto corpus =
      RequiredString(*manifest, "corpus", "manifest.json", error);
  const auto version =
      RequiredString(*manifest, "version", "manifest.json", error);
  const base::DictValue* page_corpus = manifest->FindDict("page_corpus");
  const std::string* required_page_version =
      page_corpus ? page_corpus->FindString("required_version") : nullptr;
  if (!corpus || !version || !required_page_version ||
      *required_page_version != CorpusManifest::Get().version()) {
    if (error->empty()) {
      *error = "Task Benchmark and page fixture corpus versions do not match";
    }
    return std::nullopt;
  }

  const base::ListValue* rows = manifest->FindList("scenarios");
  const base::DictValue* manifest_row = nullptr;
  if (rows) {
    for (const base::Value& value : *rows) {
      const base::DictValue* candidate = value.GetIfDict();
      if (candidate && candidate->FindString("id") &&
          *candidate->FindString("id") == scenario_id) {
        if (manifest_row) {
          *error = std::string(scenario_id) + " occurs twice in manifest.json";
          return std::nullopt;
        }
        manifest_row = candidate;
      }
    }
  }
  if (!manifest_row) {
    *error = "unknown Task Benchmark scenario " + std::string(scenario_id);
    return std::nullopt;
  }

  const auto directory =
      RequiredString(*manifest_row, "directory", scenario_id, error);
  if (!directory || directory->find('/') != std::string::npos ||
      directory->find('\\') != std::string::npos ||
      !directory->starts_with(std::string(scenario_id) + "-")) {
    *error = std::string(scenario_id) + " has an unsafe scenario directory";
    return std::nullopt;
  }
  std::optional<base::DictValue> execution = ReadObject(
      root.AppendASCII(*directory).Append(FILE_PATH_LITERAL("scenario.json")),
      error);
  if (!execution) {
    return std::nullopt;
  }

  const std::string* execution_id = execution->FindString("scenario");
  const std::string* execution_corpus = execution->FindString("corpus");
  const std::string* execution_version = execution->FindString("version");
  if (!execution_id || *execution_id != scenario_id || !execution_corpus ||
      *execution_corpus != *corpus || !execution_version ||
      *execution_version != *version) {
    *error = std::string(scenario_id) +
             " scenario.json does not match its manifest row";
    return std::nullopt;
  }

  TaskBenchmarkScenario scenario;
  scenario.id_ = std::string(scenario_id);
  const auto goal =
      RequiredString(*manifest_row, "user_goal", scenario_id, error);
  const auto outcome =
      RequiredString(*manifest_row, "expected_outcome", scenario_id, error);
  const base::DictValue* artifact = manifest_row->FindDict("expected_artifact");
  const auto artifact_kind =
      artifact ? RequiredString(*artifact, "kind", scenario_id, error)
               : std::optional<std::string>();
  const auto artifact_rule =
      artifact ? RequiredString(*artifact, "rule", scenario_id, error)
               : std::optional<std::string>();
  const base::DictValue* scripted_outputs =
      manifest->FindDict("scripted_model_outputs");
  const auto scripted_outputs_state =
      scripted_outputs
          ? RequiredString(*scripted_outputs, "state", "manifest.json", error)
          : std::optional<std::string>();
  const auto starting =
      RequiredString(*manifest_row, "starting_fixture", scenario_id, error);
  const auto fixtures =
      StringList(*manifest_row, "page_fixtures", scenario_id, error);
  const base::DictValue* scope = manifest_row->FindDict("source_scope");
  const auto origins = scope ? StringList(*scope, "origins", scenario_id, error)
                             : std::optional<std::vector<std::string>>();
  const auto audit =
      StringList(*execution, "expected_audit_events", scenario_id, error);
  if (!goal || !outcome || !artifact_kind || !artifact_rule ||
      !scripted_outputs_state || !starting || !fixtures || !origins || !audit) {
    return std::nullopt;
  }
  if (std::find(fixtures->begin(), fixtures->end(), *starting) ==
      fixtures->end()) {
    *error = scenario.id_ + " starts on a fixture outside its page list";
    return std::nullopt;
  }
  scenario.goal_ = *goal;
  scenario.expected_outcome_ = *outcome;
  scenario.expected_artifact_kind_ = *artifact_kind;
  scenario.expected_artifact_rule_ = *artifact_rule;
  scenario.scripted_model_outputs_state_ = *scripted_outputs_state;
  scenario.starting_fixture_ = *starting;
  scenario.page_fixtures_ = *fixtures;
  scenario.source_origins_ = *origins;
  scenario.expected_audit_events_ = *audit;
  if (!ReadFacts(*manifest_row, scenario.id_, &scenario.required_facts_,
                 &scenario.forbidden_fact_ids_, error) ||
      !ReadSteps(*execution, scenario.id_, &scenario.steps_,
                 &scenario.injected_events_, error)) {
    return std::nullopt;
  }
  scenario.digest_ = CanonicalDigest(*manifest_row, *execution, error);
  if (scenario.digest_.empty()) {
    return std::nullopt;
  }
  return scenario;
}

const TaskBenchmarkScenario::RequiredFact*
TaskBenchmarkScenario::FindRequiredFact(std::string_view fact_id) const {
  const auto found = std::find_if(
      required_facts_.begin(), required_facts_.end(),
      [fact_id](const RequiredFact& fact) { return fact.id == fact_id; });
  return found == required_facts_.end() ? nullptr : &*found;
}

}  // namespace taffy::test
