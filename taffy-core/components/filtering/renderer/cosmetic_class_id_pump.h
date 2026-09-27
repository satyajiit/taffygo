// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_CLASS_ID_PUMP_H_
#define TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_CLASS_ID_PUMP_H_

#include <deque>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "taffy/components/filtering/mojom/cosmetic_filter.mojom.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/web/web_dom_mutation_observer.h"

namespace blink {
class WebElement;
class WebLocalFrame;
class WebNode;
}  // namespace blink

namespace taffy::filtering {

// Walks the document for class and id tokens, watches mutations, and asks
// the browser for hide selectors in batches of 200 with a 50ms delay and
// one in-flight query.
class CosmeticClassIdPump {
 public:
  using ApplySelectorsCallback =
      base::RepeatingCallback<void(const std::vector<std::string>&)>;

  CosmeticClassIdPump(
      blink::WebLocalFrame* frame,
      mojo::AssociatedRemote<mojom::CosmeticFilterHost>& host,
      ApplySelectorsCallback apply);
  CosmeticClassIdPump(const CosmeticClassIdPump&) = delete;
  CosmeticClassIdPump& operator=(const CosmeticClassIdPump&) = delete;
  ~CosmeticClassIdPump();

  void Start();

 private:
  void OnMutations(const std::vector<blink::WebDomMutation>& mutations);
  void Walk(const blink::WebNode& node);
  void CollectFromElement(const blink::WebElement& element);
  void QueueUnseen(std::vector<std::string> classes,
                   std::vector<std::string> ids);
  void ScheduleFlush();
  void Flush();
  void OnSelectors(const std::vector<std::string>& selectors);

  raw_ptr<blink::WebLocalFrame> frame_;
  raw_ptr<mojo::AssociatedRemote<mojom::CosmeticFilterHost>> host_;
  ApplySelectorsCallback apply_;
  raw_ptr<blink::WebDomMutationObserver> observer_ = nullptr;
  base::OneShotTimer flush_timer_;
  base::flat_set<std::string> seen_classes_;
  base::flat_set<std::string> seen_ids_;
  // A document can contribute thousands of tokens. Flush consumes from the
  // front in fixed batches, so a deque avoids moving every still-pending
  // string after each browser round trip.
  std::deque<std::string> pending_classes_;
  std::deque<std::string> pending_ids_;
  bool query_in_flight_ = false;
  base::WeakPtrFactory<CosmeticClassIdPump> weak_factory_{this};
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_RENDERER_COSMETIC_CLASS_ID_PUMP_H_
