// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_TASK_BROWSER_ACTIONS_H_
#define TAFFY_BROWSER_CORE_TASK_BROWSER_ACTIONS_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_task_store_rows.h"
#include "taffy/components/intelligence/content/action_dispatcher.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom-forward.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace taffy {

class TaskBrowserActionPlatform;
class TaskSourceSelectionRegistry;

// Profile-owned task tab/source boundary. It owns browser-session provenance
// and implements only the two browser actions currently executable by tasks.
// The isolated core and Android can name an intent or platform operation, but
// neither can mint ownership or choose a different live WebContents here.
class CoreTaskBrowserActions : public BrowserActionDelegate {
 public:
  explicit CoreTaskBrowserActions(content::BrowserContext* browser_context);
  CoreTaskBrowserActions(const CoreTaskBrowserActions&) = delete;
  CoreTaskBrowserActions& operator=(const CoreTaskBrowserActions&) = delete;
  ~CoreTaskBrowserActions() override;

  content::WebContents* SelectedSavedFlowStartTab() const;
  uint64_t SavedFlowStartSelectionRevision() const;
  uint64_t RegisterTaskSourceWindow();
  void UnregisterTaskSourceWindow(uint64_t window_token);
  bool RegisterTaskSourceTab(uint64_t window_token,
                             int product_tab_id,
                             content::WebContents* web_contents,
                             bool restored_without_provenance = false);
  bool MarkAssistantCreatedTaskTab(content::WebContents* web_contents);
  void UnregisterTaskSourceTab(uint64_t window_token, int product_tab_id);
  bool SelectTaskSourceTab(uint64_t window_token,
                           int product_tab_id,
                           content::WebContents* web_contents);
  void ClearTaskSourceSelection(uint64_t window_token);
  bool ActivateTaskSourceWindow(uint64_t window_token);
  void DeactivateTaskSourceWindow(uint64_t window_token);
  bool BindTaskBrowserActionPlatform(uint64_t window_token,
                                     TaskBrowserActionPlatform* platform);
  void UnbindTaskBrowserActionPlatform(uint64_t window_token,
                                       TaskBrowserActionPlatform* platform);
  bool ClaimAssistantCreatedTaskTab(uint64_t window_token,
                                    const std::string& task_id,
                                    const std::string& action_id,
                                    content::WebContents* web_contents);
  void PrepareTaskDiscoveryTab(
      const std::string& task_id,
      const std::string& effect_id,
      const std::string& browser_session_id,
      uint32_t remaining_new_source_cap,
      base::OnceCallback<void(std::optional<std::string>)> callback);
  bool IsExactTaskDiscoveryTab(const std::string& task_id,
                               const std::string& tab_id,
                               const std::string& browser_session_id,
                               uint32_t remaining_new_source_cap,
                               content::WebContents* web_contents) const;
  std::optional<std::string> ResolveTaskSearchAddress(
      const std::string& tab_id,
      const std::string& query) const;
  std::optional<core_service::mojom::TaskConsentSourcePtr>
  IssueDiscoveredTaskSourceForAction(const std::string& task_id,
                                     const std::string& action_id);
  std::optional<core_service::mojom::TaskConsentSourcePtr>
  IssueDiscoveredTaskSourceForTab(const std::string& task_id,
                                  const std::string& tab_id);
  bool ReleaseOwnedTaskTabs(const std::string& task_id);
  // The person's own open tabs for a store read (decision 0133); empty for
  // a private profile or with no task browser available.
  std::vector<TaskStoreEntry> ListPersonTabs() const;
  std::optional<core_service::mojom::TaskTabActionResultPtr> ListOwnedTaskTabs(
      const std::string& task_id,
      const std::string& browser_session_id);
  std::optional<core_service::mojom::TaskTabActionResultPtr>
  ActivateOwnedTaskTab(
      const std::string& task_id,
      const std::string& browser_session_id,
      const core_service::mojom::TaskTabDocumentTarget& target);
  std::optional<core_service::mojom::TaskTabActionResultPtr> CloseOwnedTaskTab(
      const std::string& task_id,
      const std::string& browser_session_id,
      const core_service::mojom::TaskTabDocumentTarget& target);
  // `verdict`, when not null, receives what the person is told if the start
  // is refused (decision 0231); see
  // `TaskSourceSelectionRegistry::ResolveConsentPreview`.
  std::optional<core_service::mojom::TaskConsentPreviewPtr>
  ResolveStartTaskConsent(
      core_api::mojom::TaskTemplateId template_id,
      const core_api::mojom::TaskConsentPreview& selection_intent,
      const std::optional<std::string>& skill_offer_id = std::nullopt,
      core_api::mojom::CoreApiSubmissionStatus* verdict = nullptr);

  BrowserActionStart OpenTaskTab(
      const TaskId& task_id,
      const ActionId& action_id,
      const TabId& opener_tab_id,
      content::WebContents* opener_web_contents,
      const std::string& normalized_destination) override;
  bool StartBrowserSearch(
      const TaskId& task_id,
      const TabId& tab_id,
      content::WebContents* web_contents,
      const std::string& query,
      const std::string& normalized_destination,
      const std::optional<TaskDiscoveryCapabilityBinding>& discovery) override;
  bool StartObservedLinkNavigation(
      const TabId& tab_id,
      content::WebContents* web_contents,
      const NodeHandle& source_handle,
      const std::string& normalized_destination) override;
  bool StartTabControl(BrowserCommandType command_type,
                       const TabId& tab_id,
                       content::WebContents* web_contents) override;

 protected:
  // Two shapes of the same question. Durable-consent rehydration needs the
  // middle answer — a tab with no document of its own is not a tab that moved
  // (decision 0183) — and every other caller only ever asks "is it live".
  IssuedSourceLiveness IssuedTaskSourceLiveness(
      const core_service::mojom::TaskConsentSource& source) const;
  bool IsLiveIssuedTaskSource(
      const core_service::mojom::TaskConsentSource& source) const;
  bool HasRegisteredTaskSourceWindows() const;
  bool HasActiveTaskSourceWindow() const;
  virtual bool IsTaskBrowserAvailable() const = 0;
  virtual void OnTaskSourceWindowCountChanged() {}
  // Called once when the first window becomes active, never on a repeat
  // activation or a second window: work that waited for a window to open a
  // tab in runs from here.
  virtual void OnTaskSourceWindowActivated() {}

 private:
  const raw_ptr<content::BrowserContext> task_browser_context_;
  const bool private_task_browser_profile_;
  std::unique_ptr<TaskSourceSelectionRegistry> task_source_selections_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_TASK_BROWSER_ACTIONS_H_
