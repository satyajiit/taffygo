// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/snapshot_capability_signals.h"

#include "taffy/renderer/wire_conversions.h"
// Declared here rather than left to a transitive include. An earlier revision
// of the header included a nonexistent taffy/renderer/extraction_context.h,
// and every blink type below was arriving through whatever that path was
// standing in for — which is to say, through nothing that existed.
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_ax_context.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_element_collection.h"
#include "third_party/blink/public/web/web_form_element.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace taffy {

namespace snapshot_capability_signals {

mojom::AdapterReportPtr MakeReport(const AdapterCapability& capability,
                                   AdapterStatus status) {
  auto report = mojom::AdapterReport::New();
  report->adapter = wire::ToMojom(capability.kind);
  report->status = wire::ToMojom(status);
  report->adapter_version = capability.extraction_rule_version;
  report->extraction_rule_version = capability.extraction_rule_version;
  if (!capability.detail_code.empty()) {
    report->detail_code = std::string(capability.detail_code);
  }
  return report;
}

}  // namespace snapshot_capability_signals

DocumentCapabilitySignals CollectCapabilitySignals(
    blink::WebLocalFrame* frame,
    const BrowserSuppliedFacts& browser_facts) {
  DocumentCapabilitySignals signals;
  signals.has_browser_assigned_epoch = !browser_facts.page_epoch.empty();
  signals.has_browser_origin_set =
      !browser_facts.allowed_origin_serializations.empty();
  signals.lifecycle_is_active =
      browser_facts.lifecycle == DocumentLifecycle::kActive;

  if (!frame) {
    return signals;
  }
  const blink::WebDocument document = frame->GetDocument();
  if (document.IsNull()) {
    return signals;
  }
  signals.has_document = true;
  signals.has_body = !document.Body().IsNull();
  signals.has_selection = frame->HasSelection();

  blink::WebFrameWidget* widget = frame->FrameWidget();
  signals.viewport_geometry_available = widget && !widget->Size().IsEmpty();

  {
    blink::WebAXContext probe(document, ui::kAXModeBasic);
    if (probe.HasActiveDocument()) {
      // Required, not defensive. WebAXObject::FromWebDocument reaches the
      // accessibility cache, and building the cache's relation table DCHECKs
      // that the document is at least layout-clean:
      //
      //   [FATAL:.../ax_relation_cache.cc:58] DCHECK failed:
      //   document.Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean.
      //   Unclean document at lifecycle "kVisualUpdatePending"
      //
      // A snapshot request arrives from the browser at a time of the browser's
      // choosing, so the document is dirty whenever the frame has pending
      // style or layout work — which is most of the time just after a load.
      // MEASURED on 2026-08-19: every case in
      // renderer/test/redaction_canary_render_view_test.cc aborted the process
      // here, on the first run those tests ever had. This is the upstream call
      // that brings the lifecycle up for accessibility, and it is a no-op when
      // there is nothing to update.
      probe.UpdateAXForAllDocuments();
      signals.accessibility_available =
          !blink::WebAXObject::FromWebDocument(document).IsDetached();
    }
  }

  // A form control anywhere, inside a form element or not. The second half
  // matters: a sign-in box with no form element is the common shape, and
  // reporting "no form controls" for one would be wrong in the direction that
  // hides a credential field.
  signals.has_form_controls = !document.Forms().empty();
  if (!signals.has_form_controls && signals.has_body) {
    blink::WebElementCollection inputs = document.GetElementsByHTMLTagName(
        blink::WebString::FromUtf8("input"));
    signals.has_form_controls = !inputs.FirstItem().IsNull();
    if (!signals.has_form_controls) {
      blink::WebElementCollection selects = document.GetElementsByHTMLTagName(
          blink::WebString::FromUtf8("select"));
      signals.has_form_controls = !selects.FirstItem().IsNull();
    }
    if (!signals.has_form_controls) {
      blink::WebElementCollection areas = document.GetElementsByHTMLTagName(
          blink::WebString::FromUtf8("textarea"));
      signals.has_form_controls = !areas.FirstItem().IsNull();
    }
  }

  // Structured data: one JSON-LD script or one itemprop attribute is enough
  // for the adapter to have something to say.
  blink::WebElementCollection scripts = document.GetElementsByHTMLTagName(
      blink::WebString::FromUtf8("script"));
  for (blink::WebElement script = scripts.FirstItem(); !script.IsNull();
       script = scripts.NextItem()) {
    const std::string type = base::ToLowerASCII(
        script.GetAttribute(blink::WebString::FromUtf8("type")).Utf8());
    if (type == "application/ld+json") {
      signals.has_structured_data = true;
      break;
    }
  }
  return signals;
}

}  // namespace taffy
