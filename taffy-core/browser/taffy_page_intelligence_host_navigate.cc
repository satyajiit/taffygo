// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>

#include "content/public/browser/web_contents.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

Origin TupleOrigin(const std::string& serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = serialization;
  return origin;
}

std::optional<std::string> ResolveControlEvidenceAddress(
    content::BrowserContext* browser_context,
    const std::string& tab_id,
    mojom::TaskActionOperationKind operation) {
  if (IsTaskHistoryOperation(operation)) {
    return ResolveTaskHistoryDestination(browser_context, tab_id, operation);
  }
  if (operation != mojom::TaskActionOperationKind::kReload) {
    return std::nullopt;
  }
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(browser_context, tab_id);
  content::WebContents* web_contents =
      host ? host->observed_web_contents() : nullptr;
  if (!web_contents) {
    return std::nullopt;
  }
  const GURL& address = web_contents->GetLastCommittedURL();
  if (!address.is_valid() || !address.SchemeIsHTTPOrHTTPS() ||
      address.has_username() || address.has_password() ||
      address.spec().empty() ||
      address.spec().size() > mojom::kMaxDestinationAddressBytes) {
    return std::nullopt;
  }
  return address.spec();
}

// Builds the dispatcher command from a granted browser-owned action. The
// capability fields come from the ledger record minted at policy evaluation
// — the reducer's proposal digest is what that grant was bound to, not a
// BIP command digest computed later from the live document.
std::optional<AuthorizedBrowserCommand> BuildTaskBrowserCommand(
    content::BrowserContext* browser_context,
    const mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    const CapabilityLedger& capabilities) {
  if (!browser_context || !action.document || !action.executable ||
      deadline_monotonic_ms == 0u) {
    return std::nullopt;
  }

  std::optional<std::string> destination;
  std::optional<std::string> evidence_address;
  std::optional<CanonicalLinkOpenHandle> observed_link;
  BrowserCommandType command_type = BrowserCommandType::kNavigate;
  switch (action.executable->operation_kind) {
    case mojom::TaskActionOperationKind::kNavigate:
      destination = action.executable->destination_address;
      break;
    case mojom::TaskActionOperationKind::kHistoryBack:
      command_type = BrowserCommandType::kGoBack;
      break;
    case mojom::TaskActionOperationKind::kHistoryForward:
      command_type = BrowserCommandType::kGoForward;
      break;
    case mojom::TaskActionOperationKind::kReload:
      command_type = BrowserCommandType::kReload;
      break;
    case mojom::TaskActionOperationKind::kStopLoading:
      command_type = BrowserCommandType::kStopLoading;
      break;
    case mojom::TaskActionOperationKind::kSearch:
      command_type = BrowserCommandType::kSearch;
      destination = action.executable->destination_address;
      break;
    case mojom::TaskActionOperationKind::kTabsOpen:
      command_type = BrowserCommandType::kOpenTaskTab;
      destination = action.executable->destination_address;
      break;
    case mojom::TaskActionOperationKind::kLinkOpen:
      command_type = BrowserCommandType::kOpenObservedLink;
      observed_link =
          ReadCanonicalLinkOpenHandle(action.executable->canonical_intent);
      if (!observed_link || !action.executable->node_id ||
          observed_link->node_id != *action.executable->node_id) {
        return std::nullopt;
      }
      destination = ResolveTaskObservedLink(browser_context, *observed_link);
      if (!destination || !action.executable->destination_address ||
          *destination != *action.executable->destination_address) {
        return std::nullopt;
      }
      break;
    default:
      return std::nullopt;
  }
  const bool controls_tab =
      IsTaskTabControlOperation(action.executable->operation_kind);
  if (controls_tab) {
    if (action.executable->destination_origin ||
        action.executable->destination_address ||
        action.executable->operand_handle ||
        action.executable->transient_search_query) {
      return std::nullopt;
    }
    evidence_address = ResolveControlEvidenceAddress(
        browser_context, action.executable->tab_id,
        action.executable->operation_kind);
    if (command_type != BrowserCommandType::kStopLoading && !evidence_address) {
      return std::nullopt;
    }
  } else {
    if (!destination || destination->empty() ||
        !action.executable->destination_origin ||
        !HttpAddressOriginEquals(*destination,
                                 *action.executable->destination_origin)) {
      return std::nullopt;
    }
    evidence_address = destination;
  }

  AuthorizedBrowserCommand command;
  command.schema_version = kBipSchemaVersion;
  command.dispatch_id = DispatchId{action.dispatch_id};
  command.action_id = ActionId{action.action_id};
  command.task_id = TaskId{task_id};
  command.idempotency_key = action.idempotency_key;
  command.command_type = command_type;
  command.tab_id = TabId{action.executable->tab_id};
  if (destination) {
    command.argument = std::move(*destination);
  }
  if (observed_link) {
    Origin source_origin;
    source_origin.kind = OriginKind::kTuple;
    source_origin.serialization = observed_link->expected_origin;
    command.source_handle = NodeHandle{
        .tab_id = TabId{observed_link->tab_id},
        .frame_id = FrameId{observed_link->frame_id},
        .page_epoch = PageEpoch{observed_link->page_epoch},
        .graph_revision = observed_link->graph_revision,
        .node_id = SemanticNodeId{observed_link->node_id},
        .expected_origin = std::move(source_origin),
    };
  }
  if (command_type == BrowserCommandType::kSearch) {
    if (!action.executable->transient_search_query) {
      return std::nullopt;
    }
    command.transient_search_query = *action.executable->transient_search_query;
  }
  command.idempotency_policy = IdempotencyPolicy::kConditionallyIdempotent;
  command.principal.kind = PrincipalKind::kAssistant;
  command.absolute_deadline_monotonic_ms = deadline_monotonic_ms;

  if (command_type == BrowserCommandType::kStopLoading) {
    Postcondition stopped;
    stopped.kind = PostconditionKind::kLoadingStopped;
    command.expected_postconditions.push_back(std::move(stopped));
  } else {
    const GURL parsed_destination(*evidence_address);
    const std::optional<std::string> evidence_origin =
        HttpAddressOrigin(*evidence_address);
    if (!evidence_origin) {
      return std::nullopt;
    }
    const Origin destination_origin = TupleOrigin(*evidence_origin);
    Postcondition navigated;
    navigated.kind = PostconditionKind::kCommittedNavigation;
    navigated.allowed_origins.push_back(destination_origin);
    Destination exact_destination;
    exact_destination.url_metadata.origin = destination_origin;
    // A typed address is a request, and a site answers it where it chooses to.
    // `kFullUrl` compares the committed URL byte for byte, so any redirect at
    // all contradicted the navigation — including a same-origin one that only
    // adds a path or a query parameter. On a phone that refused
    // `https://uidai.gov.in/`, which answers 307 to `/en`, and it refused a
    // navigate to a Google search URL, which comes back with parameters of its
    // own. The claim a typed navigate can honestly make is that the commit
    // landed on the origin the address named; `allowed_origins` above carries
    // it, and a redirect that leaves that origin is still contradicted.
    //
    // A search and a task tab keep the exact URL: their destination is one the
    // browser resolved rather than one the model spelled, and neither has been
    // observed to need this.
    //
    // A reload is the same claim about a URL the browser itself supplied: the
    // tab committed a navigation on the origin it was already on. Byte
    // equality against the previously committed URL refuses every site that
    // re-routes on load — which is every single-page application, including
    // the one an errand was told to open — and the contradiction reached the
    // task as an outcome nobody could confirm, which parks it on a question
    // the person cannot answer.
    const bool address_is_a_request =
        command_type == BrowserCommandType::kNavigate ||
        command_type == BrowserCommandType::kReload ||
        // A link a page offered is an address too, and following it is a
        // request the site answers the same way. Holding a followed link to
        // byte equality meant a result whose site redirects at all could not
        // be opened (decision 0177).
        command_type == BrowserCommandType::kOpenObservedLink ||
        command_type == BrowserCommandType::kOpenTaskTab;
    // And a site answers a request at a host of its own choosing inside its own
    // registrable domain: an apex that sends you to `www`, a regional host, a
    // staging host. Refusing that is refusing the site's answer. A cross-site
    // redirect is still contradicted, and so is an http hop.
    navigated.allows_registrable_domain_siblings = address_is_a_request;
    // And a result link on a search engine is the engine's own address: the
    // request goes there and the answer comes from the site the result names.
    // Only a link a page offered gets this — a typed address was offered by
    // nobody (decision 0179).
    navigated.allows_redirected_landing =
        command_type == BrowserCommandType::kOpenObservedLink;
    exact_destination.url_metadata.disclosure =
        address_is_a_request ? UrlDisclosure::kOriginOnly
                             : UrlDisclosure::kFullUrl;
    if (!address_is_a_request) {
      exact_destination.url_metadata.url = *evidence_address;
    }
    exact_destination.url_metadata.has_query = parsed_destination.has_query();
    exact_destination.url_metadata.has_fragment = parsed_destination.has_ref();
    exact_destination.is_cross_origin =
        action.document->normalized_origin != destination_origin.serialization;
    exact_destination.opens_new_tab =
        command_type == BrowserCommandType::kOpenTaskTab;
    navigated.expected_destination = exact_destination;
    if (command_type == BrowserCommandType::kOpenTaskTab) {
      Postcondition opened;
      opened.kind = PostconditionKind::kNewTabCreated;
      opened.allowed_origins.push_back(destination_origin);
      opened.expected_destination = exact_destination;
      opened.expects_opener_reference = false;
      command.expected_postconditions.push_back(std::move(opened));
    }
    command.expected_postconditions.push_back(std::move(navigated));
  }

  command.action_digest.algorithm = DigestAlgorithm::kSha256;
  command.action_digest.value = action.proposal_digest;
  command.canonical_intent_digest =
      crypto::SHA256Hash(action.executable->canonical_intent);
  if (!capabilities.CopyRegisteredCapability(
          CapabilityReference{action.capability_id}, command.capability)) {
    return std::nullopt;
  }
  // A document with no site of its own is bound one of two ways. A discovery
  // blank carries discovery authority and every one of its facts has to match
  // it. The error document Chromium writes when an address does not answer
  // carries none, and nothing about the document authorizes the move: the
  // destination does, and it was already decided (decision 0176). What must
  // still hold in both directions is that discovery authority and a discovery
  // document go together.
  if (action.document->opaque_origin_id && command.capability.task_discovery) {
    const auto& discovery = command.capability.task_discovery;
    if (discovery->tab_id != command.tab_id ||
        discovery->frame_id.value != action.document->frame_id ||
        discovery->page_epoch.value != action.document->page_epoch ||
        discovery->opaque_origin_id != *action.document->opaque_origin_id) {
      return std::nullopt;
    }
  } else if (command.capability.task_discovery) {
    return std::nullopt;
  }
  command.capability_reference = command.capability.capability_reference;
  if (!command.dispatch_id.is_valid() || !command.action_id.is_valid() ||
      !command.task_id.is_valid() || !command.action_digest.is_valid() ||
      !command.tab_id.is_valid()) {
    return std::nullopt;
  }
  return command;
}

}  // namespace

void DispatchTaskNavigateOnTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    CapabilityLedger& capabilities,
    TaskActionCompletion callback) {
  if (!callback) {
    return;
  }
  if (!browser_context || !action.executable) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<AuthorizedBrowserCommand> command = BuildTaskBrowserCommand(
      browser_context, action, task_id, deadline_monotonic_ms, capabilities);
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(browser_context, action.executable->tab_id);
  if (!command || !host) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  host->RequestTaskBrowserCommand(std::move(*command), std::move(callback));
}

void TaffyPageIntelligenceHost::RequestTaskBrowserCommand(
    AuthorizedBrowserCommand command,
    TaskActionCompletion callback) {
  if (!service_ || !callback) {
    if (callback) {
      std::move(callback).Run(std::nullopt);
    }
    return;
  }
  service_->SubmitTaskBrowserCommand(
      std::move(command),
      base::BindOnce(
          [](TaskActionCompletion done, ActionResult result) {
            std::move(done).Run(std::move(result));
          },
          std::move(callback)));
}

}  // namespace taffy
