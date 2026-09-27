// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_ADAPTERS_DOCUMENT_METADATA_ADAPTER_H_
#define TAFFY_RENDERER_ADAPTERS_DOCUMENT_METADATA_ADAPTER_H_

#include <string_view>

#include "taffy/renderer/adapters/adapter.h"

namespace taffy {

// Document, origin, and lifecycle facts - taken from the browser, never from
// what this process thinks it knows.
//
// This is precedence item 1 of protocol section 7.6: "browser-owned committed
// navigation/lifecycle state for URL, origin, and document identity". Section
// 7.2 says the same thing from the other direction: "renderer-reported URLs
// do not override the browser broker's committed navigation record". A
// compromised renderer can report any URL it likes, so the rule this module
// encodes is simple and total: every identity value it emits arrived in the
// request the broker authored, and it emits none of its own.
//
// That makes the module look thin, and the thinness is the point. What it
// does do is worth having:
//
//   * it emits the document node every other node hangs off, so the graph has
//     a root whose identity nobody in this process chose;
//
//   * it records the sensitivity policy identifier and task purpose the
//     broker attached, as evidence, so an audit can say which policy an
//     observation was made under without the renderer interpreting either;
//
//   * it performs the one comparison a renderer is uniquely placed to make:
//     whether THIS process's view of its own security origin is inside the
//     origin set the broker said this observation covers. It never emits its
//     own origin - it reports agreement or disagreement. A disagreement means
//     the browser and the renderer are looking at different documents, which
//     is a fact the broker very much wants and cannot obtain anywhere else;
//
//   * it carries the document language, which is page-authored, allowlisted,
//     and marked as normalized rather than as fact.
//
// What it deliberately does not do: report a URL, report an origin string,
// report frame topology, or report a lifecycle state of its own. The first
// two would be renderer-authored identity. The third is browser-owned by
// protocol section 8.1. The fourth is browser-owned by section 5.5.
class DocumentMetadataAdapter final : public Adapter {
 public:
  DocumentMetadataAdapter();
  ~DocumentMetadataAdapter() override;

  AdapterKind kind() const override;
  std::string_view name() const override;
  uint32_t extraction_rule_version() const override;
  AdapterResult Run(ExtractionContext& context) override;
};

}  // namespace taffy

#endif  // TAFFY_RENDERER_ADAPTERS_DOCUMENT_METADATA_ADAPTER_H_
