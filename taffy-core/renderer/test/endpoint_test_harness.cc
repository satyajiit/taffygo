// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/test/endpoint_test_harness.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "content/public/renderer/render_frame.h"

// VERIFY AT SP-04:
//   * content::RenderViewTest::LoadHTML() and GetMainRenderFrame() spellings,
//     and whether LoadHTML() runs to a completed document synchronously. If
//     it does not, every assertion about a parsed document races the parser
//     and the harness needs an explicit wait.
//   * content::RenderFrame::GetWebFrame() at the pin.
//   * Whether a RenderViewTest builds an accessibility tree on demand. If it
//     does not, the accessibility-path assertions have to enable a mode
//     explicitly, and the capability report will honestly say the adapter is
//     unsupported until they do.

namespace taffy::test {

namespace {

// Requests every adapter this endpoint has as OPTIONAL. A required adapter
// this document cannot support makes the whole request UNSUPPORTED (protocol
// section 15), which is correct behaviour and useless for a test that wants
// to see what a page does contain.
//
// Selection and layout are named here like the rest. They could not be until
// the additive minor contract step recorded in
// taffy-core/contracts/bip/schema/bip.version.json appended SELECTION and
// LAYOUT, and naming them changes no behaviour - the builder runs both
// regardless, because they annotate rather than produce, and a caller cannot
// ask for a viewport-scoped answer while declining the adapter that decides
// what the viewport contains. It changes what the request says, which is the
// point: a test fixture that asks for less than it expects is a fixture that
// proves less than it claims.
void AddOptionalAdapters(mojom::SnapshotRequest& request) {
  for (mojom::AdapterKind kind :
       {mojom::AdapterKind::kBrowser, mojom::AdapterKind::kAccessibility,
        mojom::AdapterKind::kForms, mojom::AdapterKind::kMetadata,
        mojom::AdapterKind::kDom, mojom::AdapterKind::kSelection,
        mojom::AdapterKind::kLayout}) {
    auto requirement = mojom::AdapterRequirement::New();
    requirement->adapter = kind;
    requirement->requirement = mojom::AdapterRequirementLevel::kOptional;
    request.adapters.push_back(std::move(requirement));
  }
}

void CollectNodeStrings(const mojom::SemanticNode& node,
                        std::vector<std::string>& out) {
  if (node.name.has_value()) {
    out.push_back(node.name.value());
  }
  if (node.description.has_value()) {
    out.push_back(node.description.value());
  }
  for (const mojom::TextRunPtr& run : node.text_runs) {
    out.push_back(run->text);
  }
  if (node.value_descriptor && node.value_descriptor->normalized_value) {
    out.push_back(node.value_descriptor->normalized_value.value());
  }
  if (node.value_descriptor && node.value_descriptor->unit) {
    out.push_back(node.value_descriptor->unit.value());
  }
  if (node.value_descriptor && node.value_descriptor->currency_code) {
    out.push_back(node.value_descriptor->currency_code.value());
  }
  for (const mojom::NodeAttributePtr& attribute : node.attributes) {
    out.push_back(attribute->value);
  }
  for (const mojom::FieldEvidencePtr& evidence : node.evidence) {
    // A secret that escaped through a source locator is still a secret that
    // escaped. Protocol section 7.5 calls the locator bounded and internal;
    // that is not the same as harmless.
    out.push_back(evidence->source_locator);
  }
  if (node.destination) {
    const mojom::UrlMetadata& url = *node.destination->url_metadata;
    if (url.origin && url.origin->serialization.has_value()) {
      out.push_back(url.origin->serialization.value());
    }
    if (url.path.has_value()) {
      out.push_back(url.path.value());
    }
    if (url.url.has_value()) {
      out.push_back(url.url.value());
    }
  }
}

// `html` with a base of about:blank as the first thing in its head.
//
// A corpus page names its stylesheet and script by root-relative paths. At a
// real origin those are fetched, and a render-view test has no network: its
// loader aborts the process. Against about:blank they resolve to nothing,
// which is how they resolved before these pages were given an origin. The
// base goes inside the head rather than before the markup, so a doctype still
// comes first and the page keeps its rendering mode.
std::string WithInertBase(const std::string& html) {
  constexpr char kBase[] = "<base href='about:blank'>";
  const std::string lower = base::ToLowerASCII(html);
  for (const std::string_view tag : {"<head", "<html"}) {
    // The tag itself, and not a longer name that begins with it: `<header>`.
    size_t open = lower.find(tag);
    while (open != std::string::npos && open + tag.size() < lower.size() &&
           !base::IsAsciiWhitespace(lower[open + tag.size()]) &&
           lower[open + tag.size()] != '>') {
      open = lower.find(tag, open + 1);
    }
    if (open == std::string::npos) {
      continue;
    }
    const size_t close = lower.find('>', open);
    if (close == std::string::npos) {
      break;
    }
    return base::StrCat({std::string_view(html).substr(0, close + 1), kBase,
                         std::string_view(html).substr(close + 1)});
  }
  return base::StrCat({kBase, html});
}

}  // namespace

EndpointTestHarness::EndpointTestHarness() = default;
EndpointTestHarness::~EndpointTestHarness() = default;

void EndpointTestHarness::SetUp() {
  content::RenderViewTest::SetUp();
}

void EndpointTestHarness::TearDown() {
  endpoint_.reset();
  content::RenderViewTest::TearDown();
}

void EndpointTestHarness::LoadAndBind(const std::string& html) {
  // At the origin every request below names. A document served from
  // LoadHTML() has an opaque origin, which the document metadata adapter
  // reads as a renderer on a document the browser did not name, and since
  // decision 0207 that refuses the whole snapshot: every render-view case
  // failed on its first assertion, with nothing to say why.
  LoadHTMLWithUrlOverride(WithInertBase(html), "https://primary.taffy.test/");
  // A new document is a new epoch, and the endpoint is rebuilt with it. That
  // is the same rule TaffyRenderFrameObserver enforces in production: the old
  // endpoint is destroyed before anything can observe the new document, so no
  // handle from the previous document can resolve against this one.
  endpoint_.reset();
  page_epoch_ =
      base::StrCat({"epoch-", base::NumberToString(++epoch_ordinal_)});
  endpoint_ = std::make_unique<PageIntelligenceEndpoint>(
      GetMainRenderFrame()->GetWebFrame(), FrameId("frame-main"));
  endpoint_->OnLifecycleChanged(mojom::DocumentLifecycleState::kActive);
}

mojom::SnapshotRequestPtr EndpointTestHarness::DocumentRequest() const {
  return RequestWithScope(mojom::ObservationScope::kDocument);
}

mojom::SnapshotRequestPtr EndpointTestHarness::RequestWithScope(
    mojom::ObservationScope scope) const {
  auto request = mojom::SnapshotRequest::New();
  request->schema_version = "0.1";
  request->request_id = "req-1";
  request->profile_id = "profile-test";
  request->tab_id = "tab-test";
  request->root_frame_id = "frame-main";
  request->expected_page_epoch = page_epoch_;
  request->scope = scope;
  AddOptionalAdapters(*request);
  request->include_child_frames = false;
  // Zero means "the broker stated no preference", which the limits policy
  // reads as the process ceiling rather than as a request for nothing.
  request->max_nodes = 0;
  request->max_text_bytes = 0;
  request->max_total_bytes = 0;
  request->max_depth = 0;
  request->deadline_ms = 0;
  request->sensitivity_policy_id = "policy-test";
  request->task_purpose = "renderer-test";

  // The origin set the broker says this observation covers. The document
  // metadata adapter compares the renderer's own view against it; an empty
  // list means the check cannot run and the adapter reports itself degraded.
  auto origin = mojom::Origin::New();
  origin->kind = mojom::OriginKind::kTuple;
  // LoadAndBind() serves every page at this origin, so the adapter agrees.
  origin->serialization = "https://primary.taffy.test";
  request->allowed_origins.push_back(std::move(origin));
  return request;
}

mojom::SnapshotResultPtr EndpointTestHarness::Snapshot(
    mojom::SnapshotRequestPtr request) {
  mojom::SnapshotResultPtr captured;
  base::RunLoop run_loop;
  endpoint_->GetSnapshot(
      std::move(request),
      base::BindOnce(
          [](mojom::SnapshotResultPtr* out, base::OnceClosure quit,
             mojom::SnapshotResultPtr result) {
            *out = std::move(result);
            std::move(quit).Run();
          },
          &captured, run_loop.QuitClosure()));
  run_loop.Run();
  // Every render-view test asserts a snapshot first and says nothing else
  // when there is none, so the code the endpoint answered is named here.
  if (captured && !captured->snapshot) {
    LOG(ERROR) << "[taffy_test_snapshot_refused] code="
               << static_cast<int>(captured->code);
  }
  return captured;
}

void EndpointTestHarness::SubmitSnapshot(mojom::SnapshotRequestPtr request,
                                         mojom::SnapshotResultPtr* out) {
  endpoint_->GetSnapshot(
      std::move(request),
      base::BindOnce(
          [](mojom::SnapshotResultPtr* out, mojom::SnapshotResultPtr result) {
            *out = std::move(result);
          },
          out));
}

mojom::ResolveNodeRequestPtr EndpointTestHarness::ResolveRequest(
    const std::string& node_id,
    const std::string& epoch) const {
  auto request = mojom::ResolveNodeRequest::New();
  request->schema_version = "0.1";
  request->request_id = "resolve-1";
  // Revision zero is the initial graph baseline, not a freshness bypass. A
  // node whose precondition-relevant facts changed after creation must answer
  // STALE_GRAPH until a caller supplies the later revision it observed. The
  // store still compares epoch first, so this baseline cannot mask an epoch
  // verdict.
  request->required_graph_revision = 0;

  auto handle = mojom::NodeHandle::New();
  handle->tab_id = "tab-test";
  handle->frame_id = "frame-main";
  handle->page_epoch = epoch;
  handle->graph_revision = 0;
  handle->node_id = node_id;
  auto origin = mojom::Origin::New();
  origin->kind = mojom::OriginKind::kTuple;
  origin->serialization = "https://primary.taffy.test";
  handle->expected_origin = std::move(origin);
  request->node_handle = std::move(handle);
  return request;
}

mojom::ResolveNodeResultPtr EndpointTestHarness::Resolve(
    mojom::ResolveNodeRequestPtr request) {
  mojom::ResolveNodeResultPtr captured;
  base::RunLoop run_loop;
  endpoint_->ResolveNode(
      std::move(request),
      base::BindOnce(
          [](mojom::ResolveNodeResultPtr* out, base::OnceClosure quit,
             mojom::ResolveNodeResultPtr result) {
            *out = std::move(result);
            std::move(quit).Run();
          },
          &captured, run_loop.QuitClosure()));
  run_loop.Run();
  return captured;
}

mojom::MediaTargetResultPtr EndpointTestHarness::InspectMedia(
    const std::string& node_id,
    uint64_t required_graph_revision,
    mojom::MediaTargetKind kind) {
  auto request = mojom::MediaTargetRequest::New();
  request->request_id = "media-1";
  request->expected_page_epoch = page_epoch_;
  request->node_id = node_id;
  request->required_graph_revision = required_graph_revision;
  request->expected_kind = kind;
  request->include_loaded_caption_cues = false;

  mojom::MediaTargetResultPtr captured;
  base::RunLoop run_loop;
  endpoint_->InspectMediaTarget(
      std::move(request),
      base::BindOnce(
          [](mojom::MediaTargetResultPtr* out, base::OnceClosure quit,
             mojom::MediaTargetResultPtr result) {
            *out = std::move(result);
            std::move(quit).Run();
          },
          &captured, run_loop.QuitClosure()));
  run_loop.Run();
  return captured;
}

mojom::RendererActionResultPtr EndpointTestHarness::Execute(
    mojom::RendererActionCommandPtr command) {
  mojom::RendererActionResultPtr captured;
  base::RunLoop run_loop;
  endpoint_->ExecuteRendererAction(
      std::move(command),
      base::BindOnce(
          [](mojom::RendererActionResultPtr* out, base::OnceClosure quit,
             mojom::RendererActionResultPtr result) {
            *out = std::move(result);
            std::move(quit).Run();
          },
          &captured, run_loop.QuitClosure()));
  run_loop.Run();
  return captured;
}

// static
std::vector<std::string> EndpointTestHarness::AllStringsIn(
    const mojom::PageSnapshot& snapshot) {
  std::vector<std::string> out;
  for (const mojom::SemanticNodePtr& node : snapshot.nodes) {
    CollectNodeStrings(*node, out);
  }
  for (const mojom::SnapshotWarningPtr& warning : snapshot.warnings) {
    if (warning->detail_code.has_value()) {
      out.push_back(warning->detail_code.value());
    }
  }
  for (const mojom::AdapterReportPtr& report : snapshot.adapters) {
    if (report->detail_code.has_value()) {
      out.push_back(report->detail_code.value());
    }
  }
  if (snapshot.committed_url_metadata) {
    const mojom::UrlMetadata& url = *snapshot.committed_url_metadata;
    if (url.origin && url.origin->serialization.has_value()) {
      out.push_back(url.origin->serialization.value());
    }
    if (url.path.has_value()) {
      out.push_back(url.path.value());
    }
    if (url.url.has_value()) {
      out.push_back(url.url.value());
    }
  }
  return out;
}

// static
std::vector<std::string> EndpointTestHarness::AllStringsIn(
    const mojom::SnapshotResult& result) {
  std::vector<std::string> out;
  if (result.snapshot) {
    out = AllStringsIn(*result.snapshot);
  }
  for (const mojom::SnapshotWarningPtr& warning : result.warnings) {
    if (warning->detail_code.has_value()) {
      out.push_back(warning->detail_code.value());
    }
  }
  return out;
}

// static
const mojom::AdapterReport* EndpointTestHarness::ReportFor(
    const mojom::PageSnapshot& snapshot,
    mojom::AdapterKind kind) {
  for (const mojom::AdapterReportPtr& report : snapshot.adapters) {
    if (report->adapter == kind) {
      return report.get();
    }
  }
  return nullptr;
}

// static
std::vector<const mojom::SemanticNode*> EndpointTestHarness::NodesWithRole(
    const mojom::PageSnapshot& snapshot,
    mojom::SemanticRole role) {
  std::vector<const mojom::SemanticNode*> nodes;
  for (const mojom::SemanticNodePtr& node : snapshot.nodes) {
    if (node->role == role) {
      nodes.push_back(node.get());
    }
  }
  return nodes;
}

// static
bool EndpointTestHarness::HasState(const mojom::SemanticNode& node,
                                   mojom::NodeState state) {
  for (mojom::NodeState candidate : node.states) {
    if (candidate == state) {
      return true;
    }
  }
  return false;
}

}  // namespace taffy::test
