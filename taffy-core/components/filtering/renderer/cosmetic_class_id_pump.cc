// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/renderer/cosmetic_class_id_pump.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/components/filtering/renderer/class_id_tokens.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"

namespace taffy::filtering {
namespace {

constexpr size_t kMaxBatchTokens = 200;
constexpr size_t kSharedBatchTokens = kMaxBatchTokens / 2;
constexpr base::TimeDelta kFlushDelay = base::Milliseconds(50);

std::string Utf8(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

bool ShouldSkip(const blink::WebElement& element) {
  return element.HasHTMLTagName("iframe") ||
         element.HasHTMLTagName("script") || element.HasHTMLTagName("style") ||
         element.HasHTMLTagName("template") ||
         element.HasHTMLTagName("noscript");
}

void TakeFront(std::deque<std::string>& source,
               std::vector<std::string>& dest,
               size_t remaining) {
  const size_t n = std::min(remaining, source.size());
  dest.reserve(dest.size() + n);
  for (size_t index = 0; index < n; ++index) {
    dest.push_back(std::move(source.front()));
    source.pop_front();
  }
}

}  // namespace

CosmeticClassIdPump::CosmeticClassIdPump(
    blink::WebLocalFrame* frame,
    mojo::AssociatedRemote<mojom::CosmeticFilterHost>& host,
    ApplySelectorsCallback apply)
    : frame_(frame), host_(&host), apply_(std::move(apply)) {}

CosmeticClassIdPump::~CosmeticClassIdPump() {
  flush_timer_.Stop();
  if (observer_) {
    observer_->Disconnect();
    observer_ = nullptr;
  }
}

void CosmeticClassIdPump::Start() {
  if (!frame_) {
    return;
  }
  const blink::WebDocument document = frame_->GetDocument();
  if (document.IsNull()) {
    return;
  }
  observer_ = blink::WebDomMutationObserver::Create(
      document, base::BindRepeating(&CosmeticClassIdPump::OnMutations,
                                    weak_factory_.GetWeakPtr()));
  Walk(document.DocumentElement());
  ScheduleFlush();
}

void CosmeticClassIdPump::OnMutations(
    const std::vector<blink::WebDomMutation>& mutations) {
  for (const blink::WebDomMutation& mutation : mutations) {
    if (mutation.kind == blink::WebDomMutation::Kind::kChildList) {
      for (int added : mutation.added_dom_node_ids) {
        if (added != 0) {
          Walk(blink::WebNode::FromDomNodeId(added));
        }
      }
      continue;
    }
    if (mutation.kind != blink::WebDomMutation::Kind::kAttributes) {
      continue;
    }
    const std::string name = Utf8(mutation.attribute_name);
    if (name != "class" && name != "id") {
      continue;
    }
    blink::WebNode node =
        blink::WebNode::FromDomNodeId(mutation.target_dom_node_id);
    if (node.IsNull() || !node.IsElementNode()) {
      continue;
    }
    CollectFromElement(node.To<blink::WebElement>());
  }
}

void CosmeticClassIdPump::Walk(const blink::WebNode& node) {
  if (node.IsNull()) {
    return;
  }
  if (node.IsElementNode()) {
    const blink::WebElement element = node.To<blink::WebElement>();
    // Still collect id/class on iframes (EasyList hides those elements).
    // Do not descend: a child frame has its own observer.
    CollectFromElement(element);
    if (ShouldSkip(element)) {
      return;
    }
  }
  for (blink::WebNode child = node.FirstChild(); !child.IsNull();
       child = child.NextSibling()) {
    Walk(child);
  }
}

void CosmeticClassIdPump::CollectFromElement(
    const blink::WebElement& element) {
  if (element.IsNull()) {
    return;
  }
  if (element.HasHTMLTagName("script") || element.HasHTMLTagName("style") ||
      element.HasHTMLTagName("template") ||
      element.HasHTMLTagName("noscript")) {
    return;
  }
  std::vector<std::string> ids;
  const std::string id = Utf8(element.GetIdAttribute());
  if (!id.empty()) {
    ids.push_back(id);
  }
  QueueUnseen(SplitClassAttribute(Utf8(element.GetAttribute("class"))),
              std::move(ids));
}

void CosmeticClassIdPump::QueueUnseen(std::vector<std::string> classes,
                                      std::vector<std::string> ids) {
  std::vector<std::string> new_classes =
      TakeUnseen(seen_classes_, std::move(classes));
  std::vector<std::string> new_ids = TakeUnseen(seen_ids_, std::move(ids));
  if (new_classes.empty() && new_ids.empty()) {
    return;
  }
  pending_classes_.insert(pending_classes_.end(),
                          std::make_move_iterator(new_classes.begin()),
                          std::make_move_iterator(new_classes.end()));
  pending_ids_.insert(pending_ids_.end(),
                      std::make_move_iterator(new_ids.begin()),
                      std::make_move_iterator(new_ids.end()));
  ScheduleFlush();
}

void CosmeticClassIdPump::ScheduleFlush() {
  if (query_in_flight_ || flush_timer_.IsRunning()) {
    return;
  }
  if (pending_classes_.empty() && pending_ids_.empty()) {
    return;
  }
  flush_timer_.Start(FROM_HERE, kFlushDelay,
                     base::BindOnce(&CosmeticClassIdPump::Flush,
                                    weak_factory_.GetWeakPtr()));
}

void CosmeticClassIdPump::Flush() {
  if (query_in_flight_ || !host_ || !host_->is_bound()) {
    return;
  }
  std::vector<std::string> classes;
  std::vector<std::string> ids;
  // A class-heavy document must not postpone every id rule until the entire
  // class backlog drains. Give each non-empty family half the first pass, then
  // let either consume capacity the other did not need.
  const size_t first_class_budget =
      pending_ids_.empty() ? kMaxBatchTokens : kSharedBatchTokens;
  TakeFront(pending_classes_, classes, first_class_budget);
  TakeFront(pending_ids_, ids, kMaxBatchTokens - classes.size());
  TakeFront(pending_classes_, classes,
            kMaxBatchTokens - classes.size() - ids.size());
  if (classes.empty() && ids.empty()) {
    return;
  }
  query_in_flight_ = true;
  (*host_)->HiddenClassIdSelectors(
      std::move(classes), std::move(ids),
      base::BindOnce(&CosmeticClassIdPump::OnSelectors,
                     weak_factory_.GetWeakPtr()));
}

void CosmeticClassIdPump::OnSelectors(
    const std::vector<std::string>& selectors) {
  query_in_flight_ = false;
  if (!selectors.empty() && apply_) {
    apply_.Run(selectors);
  }
  ScheduleFlush();
}

}  // namespace taffy::filtering
