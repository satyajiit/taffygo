// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TAFFY_PAGE_INTELLIGENCE_HOST_H_
#define TAFFY_BROWSER_TAFFY_PAGE_INTELLIGENCE_HOST_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "taffy/browser/observed_link_registry.h"
#include "taffy/common/public/page_intelligence_service.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/components/security/browser/value_reference_vault.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
class Page;
class WebContents;
}  // namespace content

namespace taffy {

class TaffyPageIntelligenceHost;
class TaffyBrowserEffectSource;

using ObservationCompletion =
    base::OnceCallback<void(std::optional<ObservationEnvelope>)>;

struct DirectObservationContext {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string origin;
  std::string host;
  uint64_t graph_revision = 0;
};

// Live browser-owned document facts for a task proposal. The isolated core
// may name a task and target tab, but it cannot assert which renderer frame,
// document epoch, or committed origin currently occupies that tab.
struct TaskPolicyDocumentContext {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string origin;
  uint64_t graph_revision = 0;
};

struct TaskDiscoveryDocumentContext {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string opaque_origin_id;
};

// Live browser-owned facts for a tab whose committed document has no site of
// its own - Chromium's own error document is the ordinary case. There is no
// origin to carry, because an opaque one names nothing policy can classify
// and nothing a person could be told, so this context authorizes exactly one
// thing: a move that leaves the document (decision 0176).
struct TaskDepartureDocumentContext {
  std::string tab_id;
  std::string frame_id;
  std::string page_epoch;
  std::string opaque_origin_id;
};

// The one host observing `tab_id` in `browser_context`, or null.
//
// Null for no match and null for two, deliberately: a duplicate browser
// identity is a corruption, not a reason to pick the first `WebContents` in
// process order. Every document resolver below is written on top of this, so
// they all fail closed the same way.
TaffyPageIntelligenceHost* FindExactlyOneTabHost(
    content::BrowserContext* browser_context,
    const std::string& tab_id);

// Resolves exactly one live top-level document in `browser_context`. No
// selected-tab heuristic is used: a task proposal already names its target
// tab, and a missing or ambiguous match fails closed.
std::optional<TaskPolicyDocumentContext> ResolveTaskPolicyDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id);

// Resolves only the exact opaque about:blank document of a browser-owned
// discovery tab. This is deliberately separate from ordinary policy document
// resolution: an opaque blank is a navigation context, never a consented page.
std::optional<TaskDiscoveryDocumentContext> ResolveTaskDiscoveryDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id);

// Resolves a live top-level document whose committed origin has no site: an
// error document, a sandboxed document, anything whose origin is opaque. It
// deliberately does not require `about:blank`, because the document this
// exists for is the one Chromium writes when an address does not answer.
// Answering here is not consent to read or act on that document; it is the
// browser saying the tab is there and a move that leaves it can be bound.
std::optional<TaskDepartureDocumentContext> ResolveTaskDepartureDocument(
    content::BrowserContext* browser_context,
    const std::string& tab_id);

// Resolves the live session-history entry named by a typed back/forward
// operation. The returned address is browser-owned and exact; a missing entry,
// unsupported operation, non-http(s) URL, or ambiguous tab is a refusal.
std::optional<std::string> ResolveTaskHistoryDestination(
    content::BrowserContext* browser_context,
    const std::string& tab_id,
    core_service::mojom::TaskActionOperationKind operation);

// Resolves an opaque observed-link handle against exactly one live host in the
// named profile. The destination is browser-local and absent after a process
// restart, document invalidation, graph delta, or incomplete replacement.
std::optional<std::string> ResolveTaskObservedLink(
    content::BrowserContext* browser_context,
    const CanonicalLinkOpenHandle& handle);
// The same exact live handle lookup for an explicit download proposal.
// Download and new-tab flags are allowed; the destination must be HTTPS.
std::optional<std::string> ResolveTaskObservedDownload(
    content::BrowserContext* browser_context,
    const CanonicalObservedNodeHandle& handle);

// Revokes every browser-only observed-link destination in one profile. A core
// generation cannot inherit transient page authority from the generation that
// produced it, even when the renderer and WebContents remain alive.
void InvalidateTaskObservedLinks(content::BrowserContext* browser_context);

TaffyPageIntelligenceHost* FindPageIntelligenceHost(
    content::BrowserContext* browser_context,
    const std::string& tab_id);

// Resolves a browser-owned tab/frame/document tuple inside exactly one
// profile and requests a bounded observation. The callback receives one
// terminal envelope or nullopt if validation fails or the tab disappears.
// Synchronous validation failure returns an invalid ID after invoking the
// callback.
RequestId ObserveTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::PageObservationEffect& effect,
    ActorLeaseRegistry& actor_leases,
    CapabilityLedger& capabilities,
    std::optional<AuthorizedObservationTarget>* authorized_target,
    ObservationCompletion callback);

using TaskActionCompletion =
    base::OnceCallback<void(std::optional<ActionResult>)>;

// Dispatches one granted click or scroll on the consented tab. The callback
// receives the dispatcher result, or nullopt when the tab, grant, or envelope
// cannot be bound.
void DispatchTaskActionOnTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    CapabilityLedger& capabilities,
    TaskActionCompletion callback);

// Dispatches one granted in-tab navigation on the consented tab. The callback
// receives the dispatcher result, or nullopt when the tab, grant, or command
// cannot be bound.
void DispatchTaskNavigateOnTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    CapabilityLedger& capabilities,
    TaskActionCompletion callback);

// Cancels one exact observation in one exact profile/tab. A stale or foreign
// request ID is ignored rather than rebound to another host.
bool CancelTabObservationForCore(content::BrowserContext* browser_context,
                                 const std::string& tab_id,
                                 RequestId request_id);

// The per-tab owner of TaffyGo's page intelligence.
//
// # Why this class exists at all
//
// PageIntelligenceBroker is a WebContentsUserData and had no production
// caller: every CreateForWebContents in the tree was in a test. A broker that
// is never created never observes navigation, never mints a PageEpoch and
// never binds the renderer's channel-associated PageIntelligence remote — so
// the renderer observer patch 0007 constructs would serve an interface nobody
// asks for. This class is the thing that asks, and
// chromium/patches/0023-attach-page-intelligence-to-tabs.md is the one
// upstream line that calls it.
//
// # What it owns
//
//   * the broker, through CreateForWebContents;
//   * the per-tab PageIntelligenceServiceImpl;
//   * the result and observability sinks the service needs. The task journal is
//     profile-owned and injected; this tab never substitutes a counter for a
//     durable dispatch decision.
//
// # Attachment is not observation
//
// AttachTabHelpers runs for thin webviews and for tabs alike, and content
// offers this directory no way to tell them apart —
// //taffy/DEPS forbids naming //chrome, which is where TabAndroid
// lives. So this class attaches to both and that is deliberate rather than
// overlooked: attaching costs one WebContentsObserver and produces nothing on
// its own, because every observation is a request somebody has to make. The
// refusal that matters is at request time, in frame_inclusion_policy.cc and
// in the policy grant, both of which this directory owns and unit-tests.
//
// UI thread only.
class TaffyPageIntelligenceHost
    : public content::WebContentsUserData<TaffyPageIntelligenceHost>,
      public content::WebContentsObserver,
      public PageIntelligenceResultSink,
      public ObservabilitySink {
 public:
  using DownloadDirectoryResolver = base::RepeatingCallback<base::FilePath()>;

  TaffyPageIntelligenceHost(const TaffyPageIntelligenceHost&) = delete;
  TaffyPageIntelligenceHost& operator=(const TaffyPageIntelligenceHost&) =
      delete;
  ~TaffyPageIntelligenceHost() override;

  // The one production entry point, called from tab assembly. Idempotent, and
  // safe with a null argument. Refuses an inner WebContents — a guest view or
  // an embedded contents is not a page a person is browsing, and its outer
  // contents already has a host.
  static void AttachIfEligible(content::WebContents* web_contents);

  // The production composition entry point. Chrome owns the profile's live
  // download preference, so it supplies a resolver while attaching the
  // otherwise content-only host. An empty result is honest unknown state, not
  // permission to substitute the process-wide platform default.
  static void AttachIfEligible(
      content::WebContents* web_contents,
      DownloadDirectoryResolver default_download_directory);

  // The same attachment with the profile's authority named explicitly.
  // AttachIfEligible resolves it from the profile-keyed CoreServiceManager and
  // calls this; a test that has no profile — which is every unit test in this
  // directory — passes its own. All three must outlive the WebContents, which
  // the profile's own do by construction.
  //
  // `value_references` may be null, and a tab attached without one refuses
  // every envelope that names a held value rather than dispatching one with
  // nothing in it. It is the third rather than folded into the pair because it
  // is not authority: the ledgers decide whether an action may happen, and the
  // vault decides only whether the browser may be holding the bytes it would
  // carry (decision 0063 section 3).
  static void AttachWithAuthority(
      content::WebContents* web_contents,
      ActorLeaseRegistry* actor_leases,
      CapabilityLedger* capabilities,
      ValueReferenceVault* value_references,
      BrowserActionDelegate* browser_actions = nullptr,
      TaskJournalSink* task_journal = nullptr,
      DownloadDirectoryResolver default_download_directory = {});

  // Whether this WebContents was eligible, without attaching. Exposed so the
  // rule can be tested directly rather than inferred from a side effect.
  static bool IsEligible(content::WebContents* web_contents);

  PageIntelligenceService* service() { return service_.get(); }

  // The tab this host belongs to. Public because the process-wide directory
  // of hosts lives in an anonymous namespace in the .cc and has to be able to
  // ask each one which tab it is, and WebContentsUserData's own accessor is
  // protected. It hands out no authority: every caller in the tree uses it to
  // read visibility and frame liveness.
  content::WebContents* observed_web_contents();

  // True only while this tab's action dispatcher has an unsettled dispatch
  // window. It exposes no watermark or action identity: navigation/download
  // policy needs only the fail-closed answer to whether an otherwise
  // unprivileged page flow overlaps assistant work.
  bool HasOpenAssistantDispatch() const;

  RequestId RequestObservation(
      const core_service::mojom::PageObservationEffect& effect,
      ActorLeaseRegistry& actor_leases,
      CapabilityLedger& capabilities,
      std::optional<AuthorizedObservationTarget>* authorized_target,
      ObservationCompletion callback);
  bool CancelObservation(RequestId request_id);
  void RequestTaskAction(AuthorizedActionEnvelope envelope,
                         TaskActionCompletion callback);
  void RequestTaskBrowserCommand(AuthorizedBrowserCommand command,
                                 TaskActionCompletion callback);

  // The browser's own re-read of one node in this tab. Forwarded to the
  // service's dispatcher so that every browser-side answer about what a node
  // is comes from the one ResolveNode call; see the service's own comment.
  void ResolveNodeFacts(
      const NodeHandle& handle,
      base::OnceCallback<void(std::optional<ResolvedNodeFacts>)> on_resolved);

  // The committed tuple origin of this tab's primary main frame, or empty for
  // an opaque origin or a dead renderer.
  std::string ObservableOrigin() const;
  std::optional<DirectObservationContext> BuildDirectObservationContext();
  std::optional<std::string> ResolveObservedLink(
      const CanonicalLinkOpenHandle& handle);
  std::optional<std::string> ResolveObservedDownload(
      const CanonicalObservedNodeHandle& handle);

  // Content-free counters. They are what a bring-up run reads, and they are
  // deliberately the only thing this class reports: a count of observations is
  // not a record of what was on a page.
  uint64_t observations_seen() const { return observations_seen_; }
  uint64_t observations_submitted() const { return observations_submitted_; }
  uint64_t observations_refused() const { return observations_refused_; }

  // PageIntelligenceResultSink:
  void OnProtocolSupport(ProtocolSupportEnvelope result) override;
  void OnObservationResult(ObservationEnvelope result) override;
  void OnSubscriptionResult(SubscriptionEnvelope result) override;
  void OnActionResult(ActionResult result) override;
  void OnDelta(DeltaEnvelope delta) override;
  void OnPageInvalidated(InvalidationNotice notice) override;
  void OnBackpressure(BackpressureNotice notice) override;
  void OnActorLeasePreempted(ActorLeaseId lease_id, TabId tab_id) override;

  // ObservabilitySink:
  void RecordObservation(const ObservationRecord& record) override;
  void RecordAction(const ActionRecord& record) override;
  void RecordSubscription(const SubscriptionRecord& record) override;

  // WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<TaffyPageIntelligenceHost>;
  friend class DownloadAuthorityTestPeer;
  friend class TaffyPageIntelligenceHostTestPeer;
  friend void InvalidateTaskObservedLinks(
      content::BrowserContext* browser_context);
  TaffyPageIntelligenceHost(
      content::WebContents* web_contents,
      ActorLeaseRegistry* actor_leases,
      CapabilityLedger* capabilities,
      ValueReferenceVault* value_references,
      BrowserActionDelegate* browser_actions,
      TaskJournalSink* task_journal,
      DownloadDirectoryResolver default_download_directory);

  void DeliverObservation(ObservationEnvelope result,
                          ObservationCompletion callback);
  bool ObservedLinkHandleIsCurrent(const CanonicalObservedNodeHandle& handle);

  // Declared before the service so reverse destruction tears down every raw
  // resolver consumer before this transient authority disappears.
  ObservedLinkRegistry observed_links_;
  // Declared before the service because the dispatcher holds it by raw
  // pointer. Reverse destruction drops the dispatcher first, then stops the
  // profile download observations.
  std::unique_ptr<TaffyBrowserEffectSource> browser_effects_;
  std::unique_ptr<PageIntelligenceServiceImpl> service_;

  // One move-only terminal callback per in-flight generated request.
  std::map<RequestId, ObservationCompletion> pending_observations_;
  std::optional<ObservationEnvelope> synchronous_observation_result_;
  bool registering_observation_ = false;

  uint64_t observations_seen_ = 0;
  uint64_t observations_submitted_ = 0;
  uint64_t observations_refused_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TAFFY_PAGE_INTELLIGENCE_HOST_H_
