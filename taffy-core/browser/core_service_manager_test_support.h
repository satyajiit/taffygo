// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_TEST_SUPPORT_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_TEST_SUPPORT_H_

#include <memory>

#include "base/memory/scoped_refptr.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/profile_store/profile_store_reader.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy::test {

// The tail of a test-built CoreServiceManager: every broker and plane the
// constructor CHECKs into existence but a manager-sequencing suite never
// exercises. Suites keep constructing their interesting head pieces — the
// tool supervisor, the observation broker, the effect broker — and hand them
// here instead of repeating the quiet tail at every construction site, which
// is how five suites silently fell out of step with the constructor across
// two phases.
//
// Everything quiet refuses rather than crashes: the account plane rides the
// test context, the provider auth broker and model broker have no URL loader
// factory, the asset plane has no directory and no origin, and the
// filtering service reads an absent list over this object's
// own pref service. That pref service is the manager's too — it carries every
// taffy profile preference, so a suite can write one and see the manager read
// it. Declare the tail before the manager it builds: both the filtering
// service and the manager hold these prefs for the manager's lifetime.
class QuietManagerTail {
 public:
  QuietManagerTail();
  QuietManagerTail(const QuietManagerTail&) = delete;
  QuietManagerTail& operator=(const QuietManagerTail&) = delete;
  ~QuietManagerTail();

  std::unique_ptr<CoreServiceManager> MakeManager(
      content::BrowserContext* context,
      std::unique_ptr<CoreStorageBroker> storage_broker,
      std::unique_ptr<ProfileToolSupervisor> tool_supervisor,
      scoped_refptr<CorePageObservationBroker> page_observation_broker,
      std::unique_ptr<CoreEffectBroker> effect_broker,
      std::unique_ptr<ProfileSavedDataBroker> saved_data_broker = nullptr,
      std::unique_ptr<ProfileStoreReader> profile_store_reader = nullptr);

 private:
  TestingPrefServiceSimple prefs_;
};

}  // namespace taffy::test

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_TEST_SUPPORT_H_
