// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_policy_destination.h"

#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_canonical_intent.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

using Resolved =
    base::expected<TaskPolicyDestination, service_mojom::TaskActionResultCode>;

// Pairs a resolved address with the tuple origin derived from it. An address
// with no http(s) origin is not a destination this policy scope can name.
Resolved DestinationFor(std::optional<std::string> address,
                        service_mojom::TaskActionResultCode absent) {
  if (!address) {
    return base::unexpected(absent);
  }
  std::optional<std::string> origin = HttpAddressOrigin(*address);
  if (!origin) {
    return base::unexpected(
        service_mojom::TaskActionResultCode::kEgressNotAuthorized);
  }
  return TaskPolicyDestination{.address = std::move(*address),
                               .origin = std::move(*origin)};
}

}  // namespace

bool TaskSourceOriginStillNamesTheSite(const std::string& source_origin,
                                       const std::string& live_origin) {
  if (source_origin.empty() || live_origin.empty()) {
    return false;
  }
  if (source_origin == live_origin) {
    return true;
  }
  const GURL source(source_origin);
  const GURL live(live_origin);
  // `INCLUDE_PRIVATE_REGISTRIES` is the stricter reading, and it is the one
  // `SameRegistrableDomain` in postcondition_checks.cc uses for the same
  // question about a navigation's destination: two hosts under a shared pages
  // domain stay separate sites. The port has to match too, because a different
  // port is a different origin for every purpose this product has.
  return source.is_valid() && live.is_valid() &&
         source.SchemeIs(url::kHttpsScheme) &&
         live.SchemeIs(url::kHttpsScheme) &&
         source.EffectiveIntPort() == live.EffectiveIntPort() &&
         net::registry_controlled_domains::SameDomainOrHost(
             source, live,
             net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

bool TaskPolicyOperationCarriesDestination(
    service_mojom::TaskActionOperationKind operation) {
  return IsTaskNavigationOperation(operation) ||
         IsTaskSearchOperation(operation) ||
         IsTaskTabOpenOperation(operation) ||
         operation == service_mojom::TaskActionOperationKind::kDownloadStart ||
         IsTaskLinkOpenOperation(operation);
}

bool TaskPolicyOperationLeavesDocument(
    service_mojom::TaskActionOperationKind operation) {
  return IsTaskNavigationOperation(operation) ||
         IsTaskSearchOperation(operation) || IsTaskTabOpenOperation(operation);
}

Resolved ResolveTaskPolicyDestination(
    content::BrowserContext* browser_context,
    const service_mojom::TaskPolicyEffect& effect,
    const TaskPolicyDocumentContext* live,
    base::FunctionRef<std::optional<std::string>(const std::string& tab_id,
                                                 const std::string& query)>
        resolve_search) {
  if (IsTaskSearchOperation(effect.operation_kind)) {
    if (!effect.transient_search_query) {
      return base::unexpected(
          service_mojom::TaskActionResultCode::kUnsupported);
    }
    return DestinationFor(
        resolve_search(effect.tab_id, *effect.transient_search_query),
        service_mojom::TaskActionResultCode::kUnsupported);
  }
  if (IsTaskTabOpenOperation(effect.operation_kind)) {
    return DestinationFor(
        effect.destination_address,
        service_mojom::TaskActionResultCode::kEgressNotAuthorized);
  }
  if (effect.operation_kind ==
          service_mojom::TaskActionOperationKind::kDownloadStart &&
      effect.tool_name == "browser.download.from_link") {
    const auto link = ReadCanonicalDownloadLinkIntent(effect.canonical_intent);
    if (!live || !link || !effect.node_id ||
        link->target.tab_id != effect.tab_id ||
        link->target.node_id != *effect.node_id ||
        link->target.frame_id != live->frame_id ||
        link->target.page_epoch != live->page_epoch ||
        link->target.graph_revision != live->graph_revision ||
        link->target.expected_origin != live->origin) {
      return base::unexpected(service_mojom::TaskActionResultCode::kStaleGraph);
    }
    auto destination = DestinationFor(
        ResolveTaskObservedDownload(browser_context, link->target),
        service_mojom::TaskActionResultCode::kNodeGone);
    if (destination.has_value() &&
        !GURL(destination->address).SchemeIs("https")) {
      return base::unexpected(
          service_mojom::TaskActionResultCode::kEgressNotAuthorized);
    }
    return destination;
  }
  // A typed address the model names is a lead (decision 0106 section 2), and
  // the browser going there is what makes it a source: the destination is the
  // address's own origin, not the document's, so a navigate may leave the
  // site it is on. Where it lands is still policy's question, still counted
  // against the sites budget when the outcome comes back, and https only —
  // the same bound a download carries, because both put the model's words on
  // the wire as a request.
  if (IsTaskNavigationOperation(effect.operation_kind) ||
      effect.operation_kind ==
          service_mojom::TaskActionOperationKind::kDownloadStart) {
    Resolved destination = DestinationFor(
        effect.destination_address,
        service_mojom::TaskActionResultCode::kEgressNotAuthorized);
    if (destination.has_value() &&
        !GURL(destination->address).SchemeIs("https")) {
      return base::unexpected(
          service_mojom::TaskActionResultCode::kEgressNotAuthorized);
    }
    return destination;
  }
  if (IsTaskLinkOpenOperation(effect.operation_kind)) {
    // A link handle is exact only against the live document it was observed
    // in; a discovery tab has no such document and never carries one.
    const std::optional<CanonicalLinkOpenHandle> handle =
        ReadCanonicalLinkOpenHandle(effect.canonical_intent);
    // Which clause refused, as a compiled-in name. Nine reach the model as
    // one `kStaleGraph`, and "the page changed under you" is the right
    // sentence for only some of them.
    const char* at = nullptr;
    if (!live) {
      at = "no-live-document";
    } else if (!handle) {
      at = "intent-carries-no-handle";
    } else if (!effect.node_id) {
      at = "no-node";
    } else if (handle->tab_id != effect.tab_id) {
      at = "tab";
    } else if (handle->node_id != *effect.node_id) {
      at = "node";
    } else if (handle->frame_id != live->frame_id) {
      at = "frame";
    } else if (handle->page_epoch != live->page_epoch) {
      at = "page-epoch";
    } else if (handle->graph_revision > live->graph_revision) {
      // A handle naming a revision the browser has never reported is not a
      // handle this process minted. A handle naming an *earlier* one is an
      // ordinary read of a page that has since changed, and whether this
      // table can still vouch for that node's address is the registry's
      // question — it answers it with a window, from the snapshot that
      // populated the table to the newest delta absorbed (decision 0156
      // clause 3), and `ObservedLinkHandleIsCurrent` two calls below asks
      // exactly that.
      //
      // This clause required equality, which is a third copy of the rule and
      // the strictest of the three, so it refused before the one that owns it
      // was ever asked. Measured on a phone: `[taffy_link_open_refused]
      // at=revision` on the one move an errand has for following a search
      // result, on a results page that mutates between the reading the model
      // saw and the tap it asked for — which is every results page.
      at = "revision-from-the-future";
    } else if (handle->expected_origin_is_opaque) {
      at = "opaque-origin";
    } else if (handle->expected_origin != live->origin) {
      at = "origin";
    }
    if (at) {
      LOG(WARNING) << "[taffy_link_open_refused] at=" << at;
      return base::unexpected(service_mojom::TaskActionResultCode::kStaleGraph);
    }
    return DestinationFor(ResolveTaskObservedLink(browser_context, *handle),
                          service_mojom::TaskActionResultCode::kNodeGone);
  }
  return base::unexpected(service_mojom::TaskActionResultCode::kUnsupported);
}

}  // namespace taffy
