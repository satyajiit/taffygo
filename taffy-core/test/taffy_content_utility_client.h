// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_TAFFY_CONTENT_UTILITY_CLIENT_H_
#define TAFFY_TEST_TAFFY_CONTENT_UTILITY_CLIENT_H_

#include "content/public/utility/content_utility_client.h"

namespace taffy::test {

// The utility half of `taffy_browsertests`.
//
// It installs the **product's own** service registry —
// `taffy::RegisterUtilityMainThreadServices`, the same function
// `ChromeContentUtilityClient` calls — so a worker this binary launches is the
// worker the product launches, in a real sandboxed process, and not a test
// double standing where one would be.
//
// The distinction this directory's README draws about Chrome still holds and is
// not weakened here: a Chrome `Profile` has no substitute, which is why the
// core-service recovery suite lives under `//taffy/test/recovery` and runs
// inside Chrome. A tool worker has no profile in it at all — it is handed a
// job, the descriptors the browser opened, and a pipe — so the composition it
// needs is a browser process with this registry, which is exactly what this
// class makes true.
class TaffyContentUtilityClient : public content::ContentUtilityClient {
 public:
  TaffyContentUtilityClient();
  TaffyContentUtilityClient(const TaffyContentUtilityClient&) = delete;
  TaffyContentUtilityClient& operator=(const TaffyContentUtilityClient&) =
      delete;
  ~TaffyContentUtilityClient() override;

  // content::ContentUtilityClient:
  void RegisterMainThreadServices(mojo::ServiceFactory& services) override;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_TAFFY_CONTENT_UTILITY_CLIENT_H_
