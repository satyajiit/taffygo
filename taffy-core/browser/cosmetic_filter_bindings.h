// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_COSMETIC_FILTER_BINDINGS_H_
#define TAFFY_BROWSER_COSMETIC_FILTER_BINDINGS_H_

#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "taffy/components/filtering/mojom/cosmetic_filter.mojom.h"

namespace content {
class RenderFrameHost;
}

namespace taffy {

// The one entry patch 0036 calls from the associated-interface registry.
// Looks up the profile's filtering service with GetForProfileIfExists and
// never constructs the core-service manager.
void BindCosmeticFilterHost(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<filtering::mojom::CosmeticFilterHost>
        receiver);

}  // namespace taffy

#endif  // TAFFY_BROWSER_COSMETIC_FILTER_BINDINGS_H_
