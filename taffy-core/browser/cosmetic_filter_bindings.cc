// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/cosmetic_filter_bindings.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/components/filtering/browser/cosmetic_filter_host.h"
#include "taffy/components/filtering/browser/filtering_ruleset_service.h"

namespace taffy {

void BindCosmeticFilterHost(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingAssociatedReceiver<filtering::mojom::CosmeticFilterHost>
        receiver) {
  if (!render_frame_host) {
    return;
  }
  Profile* profile =
      Profile::FromBrowserContext(render_frame_host->GetBrowserContext());
  if (!profile) {
    return;
  }
  // GetForProfileIfExists on purpose: a cosmetic query must never construct
  // a profile's core-service manager.
  CoreServiceManager* manager =
      CoreServiceManagerFactory::GetForProfileIfExists(profile);
  if (!manager || !manager->filtering_service()) {
    return;
  }
  filtering::CosmeticFilterHost::Bind(
      render_frame_host, manager->filtering_service()->GetWeakPtr(),
      std::move(receiver));
}

}  // namespace taffy
