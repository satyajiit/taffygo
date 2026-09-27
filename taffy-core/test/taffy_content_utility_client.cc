// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/taffy_content_utility_client.h"

#include "taffy/utility/service_factory.h"

namespace taffy::test {

TaffyContentUtilityClient::TaffyContentUtilityClient() = default;

TaffyContentUtilityClient::~TaffyContentUtilityClient() = default;

void TaffyContentUtilityClient::RegisterMainThreadServices(
    mojo::ServiceFactory& services) {
  RegisterUtilityMainThreadServices(services);
}

}  // namespace taffy::test
