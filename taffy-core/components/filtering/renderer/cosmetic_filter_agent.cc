// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/cosmetic_filter_agent.h"

#include <utility>

#include "base/functional/bind.h"
#include "taffy/components/filtering/renderer/hide_stylesheet.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_css_origin.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace taffy::filtering {

CosmeticFilterAgent::CosmeticFilterAgent(
    blink::WebLocalFrame* frame,
    mojo::AssociatedRemote<mojom::CosmeticFilterHost>& host)
    : frame_(frame), host_(&host) {}

CosmeticFilterAgent::~CosmeticFilterAgent() = default;

void CosmeticFilterAgent::Start() {
  if (!host_ || !host_->is_bound()) {
    return;
  }
  (*host_)->GetUrlCosmeticResources(base::BindOnce(
      &CosmeticFilterAgent::OnResources, weak_factory_.GetWeakPtr()));
}

void CosmeticFilterAgent::OnResources(mojom::CosmeticResourcesPtr resources) {
  if (!resources || !resources->enabled) {
    return;
  }
  InsertHideStylesheet(resources->hide_selectors);
  if (resources->generichide || !frame_ || !host_) {
    return;
  }
  pump_ = std::make_unique<CosmeticClassIdPump>(
      frame_, *host_,
      base::BindRepeating(&CosmeticFilterAgent::InsertHideStylesheet,
                          weak_factory_.GetWeakPtr()));
  pump_->Start();
}

void CosmeticFilterAgent::InsertHideStylesheet(
    const std::vector<std::string>& selectors) {
  if (!frame_) {
    return;
  }
  const std::string css = BuildHideStylesheet(selectors);
  if (css.empty()) {
    return;
  }
  blink::WebDocument document = frame_->GetDocument();
  if (document.IsNull()) {
    return;
  }
  document.InsertStyleSheet(blink::WebString::FromUtf8(css), nullptr,
                            blink::WebCssOrigin::kUser);
}

}  // namespace taffy::filtering
