// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_H_
#define TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_H_

#include <stdint.h>

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/browser/accepted_approval_ledger.h"
#include "taffy/browser/core_task_store_rows.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
class WebContents;
}  // namespace content

namespace taffy {

using TaskDiscoveryTabCallback =
    base::OnceCallback<void(content::WebContents*)>;
using TaskDiscoveryPreparationCallback =
    base::OnceCallback<void(std::optional<std::string>)>;

// Provenance is supplied only by the product TabModel owner. Chromium launch
// types are deliberately not used: none of them means "created by Taffy for a
// task", and treating a nearby value as that fact would make user tabs and
// assistant-created tabs indistinguishable at the authority boundary.
enum class TaskSourceTabProvenance : uint8_t {
  kUserOwned = 0,
  kAssistantCreated = 1,
  // Session-restored tabs carry no trustworthy task ownership marker. They
  // are never guessed to be user-owned and therefore cannot become a consent
  // source merely because the restored selector selected one.
  kUnverifiedRestored = 2,
};

// One product window's Android-owned mechanics. The registry decides which
// exact window and tab an operation may reach; implementations only perform
// that already-bound operation through the ordinary product TabModel.
class TaskBrowserActionPlatform {
 public:
  virtual ~TaskBrowserActionPlatform() = default;
  virtual std::optional<std::string> ResolveSearchAddress(
      const std::string& query) = 0;
  virtual content::WebContents* OpenTaskTab(
      const std::string& task_id,
      const std::string& action_id,
      const std::string& destination_address) = 0;
  // Builds an opener-free opaque about:blank tab and invokes the same native
  // pre-publication claim as OpenTaskTab. The callback runs exactly once, only
  // after that blank commits or with null on failure. Keeping this a separate
  // method makes it impossible to broaden the exact-http tab creator into
  // accepting opaque URLs for ordinary task actions.
  virtual void OpenTaskDiscoveryTab(const std::string& task_id,
                                    const std::string& effect_id,
                                    TaskDiscoveryTabCallback callback) = 0;
  virtual bool StartSearch(content::WebContents* web_contents,
                           const std::string& query,
                           const std::string& destination_address) = 0;
  virtual bool ActivateTaskTab(content::WebContents* web_contents) = 0;
  virtual bool CloseTaskTab(content::WebContents* web_contents) = 0;
};

// Profile-scoped proof that a source selected in Android belongs to the
// product's real TabModel. Android contributes only product ownership and
// selection. Provenance, the BIP tab identity, and the committed tuple origin
// are always derived from browser-owned WebContents state.
class TaskSourceSelectionRegistry final {
 public:
  using WindowToken = uint64_t;

  explicit TaskSourceSelectionRegistry(
      content::BrowserContext* browser_context);
  TaskSourceSelectionRegistry(const TaskSourceSelectionRegistry&) = delete;
  TaskSourceSelectionRegistry& operator=(const TaskSourceSelectionRegistry&) =
      delete;
  ~TaskSourceSelectionRegistry();

  // The future task-tab executor must call this before publishing a created
  // WebContents into a product TabModel. Provenance then lives on the browser
  // object itself; Android and Compose cannot assert it. RegisterProductTab
  // also calls it for a restored tab the profile's register names, so the
  // marker and the registry never disagree about a registered tab (decision
  // 0232); that path sets no creating task.
  static void MarkAssistantCreatedTab(content::WebContents* web_contents);
  static TaskSourceTabProvenance BrowserOwnedProvenance(
      content::WebContents* web_contents);
  // Display attribution on the live browser object, never action authority.
  // Empty for a person's tab or an object with no exact creating-task claim.
  static std::string BrowserOwnedTaskId(content::WebContents* web_contents);

  // Whether this profile's own register says Taffy created the tab with this
  // Android id, or nothing when the register cannot be read (decision 0151).
  //
  // Nothing rather than false, because the two are different answers: a
  // register that reads back empty says Taffy created no tab, and one that
  // cannot be read says nothing at all — and a restored tab nothing can be
  // said about is refused rather than admitted.
  std::optional<bool> RememberedAssistantCreatedTab(int product_tab_id) const;
  // Records that Taffy created the tab with this Android id, so the next
  // launch can still tell it from a tab a person opened.
  void RememberAssistantCreatedTab(int product_tab_id);

  WindowToken RegisterProductWindow();
  void UnregisterProductWindow(WindowToken window_token);
  bool RegisterProductTab(WindowToken window_token,
                          int product_tab_id,
                          content::WebContents* web_contents,
                          bool restored_without_provenance = false);
  void UnregisterProductTab(WindowToken window_token, int product_tab_id);
  bool SelectProductTab(WindowToken window_token,
                        int product_tab_id,
                        content::WebContents* web_contents);
  void ClearProductSelection(WindowToken window_token);
  bool ActivateProductWindow(WindowToken window_token);
  void DeactivateProductWindow(WindowToken window_token);

  bool BindBrowserActionPlatform(WindowToken window_token,
                                 TaskBrowserActionPlatform* platform);
  void UnbindBrowserActionPlatform(WindowToken window_token,
                                   TaskBrowserActionPlatform* platform);
  TaskBrowserActionPlatform* BrowserActionPlatformFor(
      content::WebContents* web_contents) const;

  // Called re-entrantly by the dedicated Java creator after it has built a
  // live regular tab but before TabModel.addTab publishes it. The synchronous
  // registration observer then sees the marker and associates the product id
  // with this exact task claim.
  bool ClaimAssistantCreatedTab(WindowToken window_token,
                                const std::string& task_id,
                                const std::string& action_id,
                                content::WebContents* web_contents);
  bool IsTaskOwnedTab(const std::string& task_id,
                      content::WebContents* web_contents) const;
  bool HasExactTaskTabClaim(const std::string& task_id,
                            const std::string& action_id,
                            content::WebContents* web_contents) const;
  // Creates at most one discovery blank for a task in this browser session.
  // The attempt is claimed before calling the platform, so re-entrancy and a
  // replay after a partially successful platform call cannot create a second
  // tab. Concurrent repeats join the first callback; a completed repeat
  // returns only the same still-live exact document.
  void PrepareDiscoveryTab(const std::string& task_id,
                           const std::string& effect_id,
                           const std::string& browser_session_id,
                           uint32_t remaining_new_source_cap,
                           TaskDiscoveryPreparationCallback callback);
  bool IsExactDiscoveryTab(const std::string& task_id,
                           const std::string& tab_id,
                           const std::string& browser_session_id,
                           uint32_t remaining_new_source_cap,
                           content::WebContents* web_contents) const;
  void ForgetUnpublishedTaskTab(const std::string& task_id,
                                content::WebContents* web_contents);
  std::vector<content::WebContents*> BeginReleaseTaskTabs(
      const std::string& task_id);
  void CompleteTaskTabRelease(content::WebContents* web_contents, bool closed);
  bool HasTaskTabsPendingRelease(const std::string& task_id) const;
  bool CanReconcileTaskTabs(const std::string& task_id) const;

  // Exact, bounded task-tab tools. All three operate only on live product
  // tabs carrying the browser's assistant-created marker and this task's
  // claim. The caller has already matched `browser_session_id` to the current
  // profile session; it is carried here so a verified result cannot be
  // replayed as belonging to a later session.
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

  // The person's own open tabs (decision 0133): every registered tab the
  // person opened, on a page, in this profile. Taffy's tabs, restored tabs
  // whose ownership cannot be told, blank tabs and every private tab are
  // left out here, where provenance is known; nothing above may widen it.
  std::vector<TaskStoreEntry> ListPersonTabs() const;

  // Issues an exact source only after an assistant-created task tab has a
  // live HTTP(S) document. The action form is for a newly opened tab whose
  // browser result has not yet exposed its TabId; the tab form is for a
  // verified search/navigation on an already-owned task tab. Neither method
  // grants task authority -- the accepted-consent ledger and the durable Rust
  // transition independently spend the discovery cap before the source can be
  // read.
  std::optional<core_service::mojom::TaskConsentSourcePtr>
  IssueDiscoveredSourceForAction(const std::string& task_id,
                                 const std::string& action_id);
  std::optional<core_service::mojom::TaskConsentSourcePtr>
  IssueDiscoveredSourceForTab(const std::string& task_id,
                              const std::string& tab_id);

  // `intent` contains display hosts selected by the UI. It carries no source,
  // tab, origin, or capability identity. Exactly one active product window is
  // resolved here; each host must name exactly one live user-owned tab, so an
  // ambiguous same-host pair is refused rather than guessed.
  //
  // A refusal also writes, when `verdict` is not null, what the person is
  // told (decision 0231). A request that does not fit is `kInvalidRequest`; a
  // readable request refused because of the person's tabs or windows says
  // which, because no rewording of the request can fix that.
  std::optional<core_service::mojom::TaskConsentPreviewPtr>
  ResolveConsentPreview(
      core_api::mojom::TaskTemplateId template_id,
      const core_api::mojom::TaskConsentPreview& intent,
      const std::optional<std::string>& skill_offer_id = std::nullopt,
      core_api::mojom::CoreApiSubmissionStatus* verdict = nullptr);

  // Revalidates durable consent at every service publication. Issuance must
  // belong to this browser session and the same live tab and origin. A named
  // errand source or task-owned discovery permits same-origin navigation;
  // research additionally retains its exact reviewed canonical locator.
  //
  // The tri-state is the authority; `IsLiveIssuedSource` is the question "is
  // it live right now", which is all most callers want and all any caller may
  // use to *widen* anything. Exactly one caller needs the middle answer —
  // `AcceptedApprovalLedger::RehydrateConsentSources`, to tell a tab sitting
  // on an error document from a tab a person carried to another site, because
  // it may keep a record it already holds through the first and must destroy
  // it on the second (decision 0183).
  IssuedSourceLiveness IssuedSourceLivenessOf(
      const core_service::mojom::TaskConsentSource& source) const;
  bool IsLiveIssuedSource(
      const core_service::mojom::TaskConsentSource& source) const;

  // True while any product window is registered against this profile. A
  // registered window is an open browser window, whether or not a Taffy
  // surface is currently observing.
  bool HasRegisteredWindows() const { return !windows_.empty(); }
  // True while a registered window is active — foregrounded, so that a
  // discovery tab has somewhere to open. Distinct from registration: at a
  // cold start every window is registered before any is active.
  bool HasActiveWindow() const { return !active_windows_.empty(); }

  // Exact current manual tab for a reviewed saved-flow starting-page
  // navigation.
  content::WebContents* SelectedUserOwnedTab() const;
  uint64_t selection_revision() const { return selection_revision_; }

  size_t window_count_for_testing() const { return windows_.size(); }
  size_t issued_source_count_for_testing() const {
    return issued_sources_.size();
  }
  void set_issued_source_limit_for_testing(size_t limit) {
    issued_source_limit_ = limit;
  }

 private:
  uint64_t selection_revision_ = 0u;
  struct ProductTab {
    int product_tab_id = 0;
    base::WeakPtr<content::WebContents> web_contents;
    TaskSourceTabProvenance provenance =
        TaskSourceTabProvenance::kAssistantCreated;
  };

  struct ProductWindow {
    base::flat_map<int, ProductTab> tabs;
    std::optional<int> selected_product_tab_id;
    raw_ptr<TaskBrowserActionPlatform> browser_actions = nullptr;
  };

  struct OwnedTaskTab {
    std::string task_id;
    std::string action_id;
    WindowToken window_token = 0u;
    std::optional<int> product_tab_id;
    base::WeakPtr<content::WebContents> web_contents;
    bool release_in_flight = false;
  };

  struct IssuedSource {
    std::string source_id;
    std::string tab_id;
    std::string normalized_origin;
    std::optional<std::string> canonical_locator;
    base::WeakPtr<content::WebContents> web_contents;
    // Initial WebErrand consent and task-owned discovery create this scope.
    // Keep it separate from an omitted locator, which may also protect a
    // private research URL, or a discovery locator retained for recording.
    bool origin_scoped = false;
  };

  struct DiscoveryBootstrapRecord {
    std::string task_id;
    std::string effect_id;
    std::string browser_session_id;
    uint32_t remaining_new_source_cap = 0u;
    base::WeakPtr<content::WebContents> web_contents;
    bool pending = true;
    std::vector<TaskDiscoveryPreparationCallback> callbacks;
  };

  struct ClosedTaskTab {
    std::string task_id;
    std::string browser_session_id;
    std::string tab_id;
    std::string frame_id;
    std::string page_epoch;
    uint64_t graph_revision = 0u;
  };

  bool IsRegisteredProductContents(content::WebContents* web_contents) const;
  void EraseIssuedSourcesForContents(content::WebContents* web_contents);
  bool IssuedSourceSurvivesPrune(const IssuedSource& issued) const;
  void PruneIssuedSources();
  void PruneOwnedTaskTabs();
  OwnedTaskTab* FindOwnedTaskTab(content::WebContents* web_contents);
  const OwnedTaskTab* FindOwnedTaskTab(
      content::WebContents* web_contents) const;
  core_service::mojom::TaskTabDocumentTargetPtr LiveOwnedTaskTabTarget(
      const OwnedTaskTab& owned) const;
  OwnedTaskTab* FindExactOwnedTaskTab(
      const std::string& task_id,
      const core_service::mojom::TaskTabDocumentTarget& target);
  bool IsOwnedTaskTabActive(const OwnedTaskTab& owned) const;
  void OnDiscoveryTabCreated(std::pair<std::string, std::string> bootstrap_key,
                             WindowToken window_token,
                             content::WebContents* web_contents);
  void ForgetFailedDiscoveryTab(const std::string& task_id,
                                const std::string& effect_id,
                                content::WebContents* web_contents);
  std::optional<core_service::mojom::TaskConsentSourcePtr> IssueOwnedTaskSource(
      const std::string& task_id,
      content::WebContents* web_contents);
  static std::optional<std::string> CanonicalSourceLocator(
      content::WebContents* web_contents);

  const raw_ptr<content::BrowserContext> browser_context_;
  base::flat_map<WindowToken, ProductWindow> windows_;
  base::flat_map<std::string, IssuedSource> issued_sources_;
  std::vector<OwnedTaskTab> owned_task_tabs_;
  std::deque<ClosedTaskTab> closed_task_tabs_;
  base::flat_map<std::pair<std::string, std::string>, DiscoveryBootstrapRecord>
      discovery_bootstraps_;
  base::flat_set<std::string> session_task_ids_;
  base::flat_set<WindowToken> active_windows_;
  WindowToken next_window_token_ = 1u;
  size_t issued_source_limit_ =
      core_service::mojom::kMaxTaskRevisionsPerProfile *
      core_service::mojom::kMaxTaskConsentSources;
  size_t closed_task_tab_limit_ =
      core_service::mojom::kMaxTaskRevisionsPerProfile *
      core_service::mojom::kMaxTaskTabResults;
  base::WeakPtrFactory<TaskSourceSelectionRegistry> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TASK_SOURCE_SELECTION_REGISTRY_H_
