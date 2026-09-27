// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_SERVICE_MANAGER_FACTORY_H_
#define TAFFY_BROWSER_CORE_SERVICE_MANAGER_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace content {
class BrowserContext;
}

namespace taffy {

class CoreServiceManager;

// Owns one manager for each regular Profile and an independent ephemeral
// manager for each off-the-record Profile. Incognito is deliberately not
// redirected to its original profile.
class CoreServiceManagerFactory final : public ProfileKeyedServiceFactory {
 public:
  static CoreServiceManager* GetForProfile(Profile* profile);
  static CoreServiceManager* GetForProfileIfExists(Profile* profile);
  // Physical restore safety check, not a service accessor. Quarantine must
  // hide usable managers without concealing an instance that already exists.
  // This never creates a manager and exposes neither it nor any authority.
  static bool HasExistingInstanceForProfile(Profile* profile);
  static CoreServiceManager* GetForBrowserContext(
      content::BrowserContext* browser_context);
  static CoreServiceManagerFactory* GetInstance();

  CoreServiceManagerFactory(const CoreServiceManagerFactory&) = delete;
  CoreServiceManagerFactory& operator=(const CoreServiceManagerFactory&) =
      delete;

 private:
  friend base::NoDestructor<CoreServiceManagerFactory>;

  CoreServiceManagerFactory();
  ~CoreServiceManagerFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_CORE_SERVICE_MANAGER_FACTORY_H_
