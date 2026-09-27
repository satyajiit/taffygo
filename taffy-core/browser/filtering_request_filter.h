// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_FILTERING_REQUEST_FILTER_H_
#define TAFFY_BROWSER_FILTERING_REQUEST_FILTER_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/components/filtering/mojom/request_filter.mojom.h"

namespace taffy {

// Binds the renderer's process-scoped request-filter interface. The browser
// implementation resolves the supplied frame token back to its live profile
// and document; the renderer never receives a ruleset or preference state.
void BindFilteringRequestFilter(
    int render_process_id,
    mojo::PendingReceiver<filtering::mojom::RequestFilter> receiver);

}  // namespace taffy

#endif  // TAFFY_BROWSER_FILTERING_REQUEST_FILTER_H_
