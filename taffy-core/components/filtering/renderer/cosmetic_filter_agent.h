// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_AGENT_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_AGENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/components/filtering/mojom/cosmetic_filter.mojom.h"
#include "taffy/components/filtering/renderer/cosmetic_class_id_pump.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

namespace blink {
class WebLocalFrame;
}  // namespace blink

namespace taffy::filtering {

// One document's cosmetics: fetch hide selectors, insert a user-origin
// stylesheet, and start the class/id pump unless generichide is set.
class CosmeticFilterAgent {
 public:
  CosmeticFilterAgent(blink::WebLocalFrame* frame,
                      mojo::AssociatedRemote<mojom::CosmeticFilterHost>& host);
  CosmeticFilterAgent(const CosmeticFilterAgent&) = delete;
  CosmeticFilterAgent& operator=(const CosmeticFilterAgent&) = delete;
  ~CosmeticFilterAgent();

  void Start();

 private:
  void OnResources(mojom::CosmeticResourcesPtr resources);
  void InsertHideStylesheet(const std::vector<std::string>& selectors);

  raw_ptr<blink::WebLocalFrame> frame_;
  raw_ptr<mojo::AssociatedRemote<mojom::CosmeticFilterHost>> host_;
  std::unique_ptr<CosmeticClassIdPump> pump_;
  base::WeakPtrFactory<CosmeticFilterAgent> weak_factory_{this};
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_FILTER_AGENT_H_
