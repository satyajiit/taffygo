// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/task_workflow_tools.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace taffy {
namespace {

struct ReviewedWorkflowTool {
  core_api::mojom::TaskTemplateId template_id;
  std::string_view name;
};

// One explicit reviewed bundle per model-backed template. Keep each entry in
// the machine-readable TAFFY_WORKFLOW_TOOL shape: the task-engine host test
// reads this exact table and checks it in both directions against the Rust M7
// registry and its stable allowlist groups. That makes this table the browser
// selection authority without creating a second unchecked tool vocabulary.
#define TAFFY_WORKFLOW_TOOL(template_id, tool_name) \
  {core_api::mojom::TaskTemplateId::template_id, tool_name},
// clang-format off
constexpr ReviewedWorkflowTool kReviewedModelWorkflowTools[] = {
    // Cross-source comparison: page adapters, cited exports and bounded local
    // transforms are relevant; form writes remain an errand-only authority.
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.tabs")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.search")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.dom.query")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.dom.read")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.link.open")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.dom.scroll")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.navigate")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.back")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.forward")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.reload")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.stop_loading")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.form.inspect")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.download.start")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.download.list")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.download.cancel")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "browser.selection.read")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "page.images")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "page.screenshot.inspect")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "page.video.inspect")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "page.pdf.inspect")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "library.search")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "library.save")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "library.remove")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "memory.search")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "memory.save")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "memory.update")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "memory.delete")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.markdown.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.csv.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.xlsx.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.pdf.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.docx.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "artifact.pptx.create")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "media.probe")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "media.audio.extract")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "media.frames.sample")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "media.transcode")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "core.table.reshape")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "python.execute")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "user.ask")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "user.handover")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "tool.search")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "tool.activate")
    TAFFY_WORKFLOW_TOOL(kCompareProducts, "run.spawn")

    // Evidence summaries may inspect downloaded media and produce cited
    // documents. Spreadsheet/table transforms stay with comparison and table
    // workflows; form writes stay with errands.
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.tabs")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.search")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.dom.query")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.dom.read")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.link.open")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.dom.scroll")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.navigate")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.back")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.forward")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.reload")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.stop_loading")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.form.inspect")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.download.start")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.download.list")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.download.cancel")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "browser.selection.read")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "page.images")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "page.screenshot.inspect")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "page.video.inspect")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "page.pdf.inspect")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "library.search")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "library.save")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "library.remove")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "memory.search")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "memory.save")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "memory.update")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "memory.delete")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "artifact.markdown.create")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "artifact.pdf.create")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "artifact.docx.create")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "artifact.pptx.create")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "media.probe")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "media.audio.extract")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "media.frames.sample")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "media.transcode")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "python.execute")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "user.ask")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "user.handover")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "tool.search")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "tool.activate")
    TAFFY_WORKFLOW_TOOL(kSummarizeEvidence, "run.spawn")

    // A model-backed source table stays within its one accepted source and
    // gains only inspection, table/file output and local transform tools.
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "browser.dom.query")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "browser.dom.read")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "browser.dom.scroll")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "browser.selection.read")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "page.images")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "page.screenshot.inspect")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "page.pdf.inspect")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "library.search")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "library.save")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "library.remove")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "memory.search")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "memory.save")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "memory.update")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "memory.delete")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "artifact.csv.create")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "artifact.xlsx.create")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "core.table.reshape")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "python.execute")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "user.ask")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "user.handover")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "tool.search")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "tool.activate")
    TAFFY_WORKFLOW_TOOL(kBuildSourceTable, "run.spawn")

    // Errands may fill one exact browser-classified field only after the
    // browser-private value confirmation and the ordinary action approval.
    // Select, toggle and submit remain withheld: a control does not say
    // whether it searches, sends a message, accepts terms or places an order.
    // Disclosure-only activation and the separately browser-owned download
    // flow keep their narrower meaning.
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.tabs")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.search")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.dom.query")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.dom.read")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.dom.click")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.dom.focus")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.link.open")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.dom.scroll")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.navigate")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.back")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.forward")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.reload")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.stop_loading")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.form.inspect")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.form.fill")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.download.start")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.download.from_link")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.download.list")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.download.cancel")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "browser.selection.read")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "page.images")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "page.screenshot.inspect")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "page.pdf.inspect")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "memory.search")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "memory.save")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "memory.update")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "memory.delete")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "user.ask")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "user.request_values")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "user.handover")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "tool.search")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "tool.activate")
    TAFFY_WORKFLOW_TOOL(kWebErrand, "run.spawn")
};
#undef TAFFY_WORKFLOW_TOOL

// The no-model local workflow has a separate reviewed bundle rather than an
// empty-list default. Keeping it in the same row shape also lets the Rust drift
// pin validate every name this function can return, whichever route selected
// it.
#define TAFFY_NO_MODEL_WORKFLOW_TOOL(template_id, tool_name) \
  {core_api::mojom::TaskTemplateId::template_id, tool_name},
constexpr ReviewedWorkflowTool kReviewedNoModelWorkflowTools[] = {
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kBuildSourceTable, "browser.dom.read")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.navigate")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.dom.read")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.dom.query")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.dom.click")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.dom.focus")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.link.open")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "user.handover")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.download.list")
    TAFFY_NO_MODEL_WORKFLOW_TOOL(kWebErrand, "browser.download.from_link")
};
#undef TAFFY_NO_MODEL_WORKFLOW_TOOL

struct ReviewedAttachedStoreTool {
  core_api::mojom::TaskTemplateId template_id;
  core_api::mojom::TaskAttachedStore store;
  std::string_view name;
};

// A person's own store reaches a task as one tool group, and only when the
// start names the store as attached (decision 0133). The rows keep the same
// machine-readable shape so the task-engine host test reads each as reachable
// from a reviewed template; the middle column is the store whose attachment
// selects the row. A no-model start has nothing that could call the tools and
// selects none of these.
#define TAFFY_STORE_WORKFLOW_TOOL(template_id, store, tool_name) \
  {core_api::mojom::TaskTemplateId::template_id,               \
   core_api::mojom::TaskAttachedStore::store, tool_name},
constexpr ReviewedAttachedStoreTool kReviewedAttachedStoreTools[] = {
    TAFFY_STORE_WORKFLOW_TOOL(kCompareProducts, kHistory, "person.history")
    TAFFY_STORE_WORKFLOW_TOOL(kCompareProducts, kBookmarks, "person.bookmarks")
    TAFFY_STORE_WORKFLOW_TOOL(kCompareProducts, kOpenTabs, "person.open_tabs")
    TAFFY_STORE_WORKFLOW_TOOL(kSummarizeEvidence, kHistory, "person.history")
    TAFFY_STORE_WORKFLOW_TOOL(kSummarizeEvidence, kBookmarks, "person.bookmarks")
    TAFFY_STORE_WORKFLOW_TOOL(kSummarizeEvidence, kOpenTabs, "person.open_tabs")
    TAFFY_STORE_WORKFLOW_TOOL(kBuildSourceTable, kHistory, "person.history")
    TAFFY_STORE_WORKFLOW_TOOL(kBuildSourceTable, kBookmarks, "person.bookmarks")
    TAFFY_STORE_WORKFLOW_TOOL(kBuildSourceTable, kOpenTabs, "person.open_tabs")
    TAFFY_STORE_WORKFLOW_TOOL(kWebErrand, kHistory, "person.history")
    TAFFY_STORE_WORKFLOW_TOOL(kWebErrand, kBookmarks, "person.bookmarks")
    TAFFY_STORE_WORKFLOW_TOOL(kWebErrand, kOpenTabs, "person.open_tabs")
};
#undef TAFFY_STORE_WORKFLOW_TOOL
// clang-format on

bool IsModelRoute(core_api::mojom::TaskProviderRoute route) {
  return route == core_api::mojom::TaskProviderRoute::kDirectUserKey ||
         route == core_api::mojom::TaskProviderRoute::kManagedService;
}

template <size_t ToolCount>
std::vector<std::string> ReviewedToolsForTemplate(
    core_api::mojom::TaskTemplateId template_id,
    const ReviewedWorkflowTool (&reviewed)[ToolCount]) {
  size_t count = 0u;
  for (const ReviewedWorkflowTool& tool : reviewed) {
    if (tool.template_id == template_id) {
      ++count;
    }
  }
  std::vector<std::string> tools;
  tools.reserve(count);
  for (const ReviewedWorkflowTool& tool : reviewed) {
    if (tool.template_id == template_id) {
      tools.emplace_back(tool.name);
    }
  }
  return tools;
}

// Appends the group of each attached store this template has a reviewed row
// for, once per store however many times the start named it. A store with no
// row for the template adds nothing: the table decides, not the caller.
void AppendAttachedStoreTools(
    core_api::mojom::TaskTemplateId template_id,
    const std::vector<core_api::mojom::TaskAttachedStore>& attached_stores,
    std::vector<std::string>& tools) {
  for (const ReviewedAttachedStoreTool& tool : kReviewedAttachedStoreTools) {
    if (tool.template_id != template_id ||
        std::find(attached_stores.begin(), attached_stores.end(),
                  tool.store) == attached_stores.end() ||
        std::find(tools.begin(), tools.end(), tool.name) != tools.end()) {
      continue;
    }
    tools.emplace_back(tool.name);
  }
}

}  // namespace

std::vector<std::string> WorkflowToolsForStart(
    core_api::mojom::TaskTemplateId template_id,
    core_api::mojom::TaskProviderRoute route,
    const std::vector<core_api::mojom::TaskAttachedStore>& attached_stores,
    const std::optional<std::string>& skill_version_id) {
  if (route == core_api::mojom::TaskProviderRoute::kNoModelRequired) {
    if (template_id == core_api::mojom::TaskTemplateId::kWebErrand &&
        (!skill_version_id || skill_version_id->empty())) {
      return {};
    }
    return ReviewedToolsForTemplate(template_id, kReviewedNoModelWorkflowTools);
  }
  if (!IsModelRoute(route)) {
    return {};
  }
  std::vector<std::string> tools =
      ReviewedToolsForTemplate(template_id, kReviewedModelWorkflowTools);
  AppendAttachedStoreTools(template_id, attached_stores, tools);
  return tools;
}

}  // namespace taffy
