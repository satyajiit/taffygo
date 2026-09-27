// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_UTILITY_SERVICE_FACTORY_H_
#define TAFFY_UTILITY_SERVICE_FACTORY_H_

namespace mojo {
class ServiceFactory;
}

namespace taffy {

// Registers only process-hosted Taffy services. Browser-side profile ownership
// and launch policy stay in //taffy/browser.
void RegisterUtilityMainThreadServices(mojo::ServiceFactory& services);

}  // namespace taffy

#endif  // TAFFY_UTILITY_SERVICE_FACTORY_H_
