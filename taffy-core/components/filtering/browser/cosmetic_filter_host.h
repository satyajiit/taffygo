// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_COSMETIC_FILTER_HOST_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_COSMETIC_FILTER_HOST_H_

#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/document_user_data.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "taffy/components/filtering/mojom/cosmetic_filter.mojom.h"

class GURL;

namespace url {
class Origin;
}

namespace taffy::filtering {

class FilteringRulesetService;

// Per-document cosmetics endpoint. The engine stays in this process; the
// renderer only observes class/id and applies CSS. Exceptions never come
// from the renderer.
class CosmeticFilterHost
    : public content::DocumentUserData<CosmeticFilterHost>,
      public mojom::CosmeticFilterHost {
 public:
  static void Bind(
      content::RenderFrameHost* rfh,
      base::WeakPtr<FilteringRulesetService> service,
      mojo::PendingAssociatedReceiver<mojom::CosmeticFilterHost> receiver);

  ~CosmeticFilterHost() override;

  void GetUrlCosmeticResources(
      GetUrlCosmeticResourcesCallback callback) override;
  void HiddenClassIdSelectors(const std::vector<std::string>& classes,
                              const std::vector<std::string>& ids,
                              HiddenClassIdSelectorsCallback callback) override;

  // Decision helpers the tests drive without a RenderFrameHost.
  static mojom::CosmeticResourcesPtr EvaluateUrlResources(
      FilteringRulesetService* service,
      const GURL& document_url,
      const url::Origin& document_origin,
      std::vector<std::string>* exceptions_out);

  static std::vector<std::string> EvaluateHiddenClassIdSelectors(
      FilteringRulesetService* service,
      bool enabled,
      bool generichide,
      const std::vector<std::string>& exceptions,
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids);

 private:
  CosmeticFilterHost(
      content::RenderFrameHost* rfh,
      base::WeakPtr<FilteringRulesetService> service,
      mojo::PendingAssociatedReceiver<mojom::CosmeticFilterHost> receiver);

  friend class content::DocumentUserData<CosmeticFilterHost>;
  DOCUMENT_USER_DATA_KEY_DECL();

  base::WeakPtr<FilteringRulesetService> service_;
  mojo::AssociatedReceiver<mojom::CosmeticFilterHost> receiver_{this};
  bool enabled_ = false;
  bool generichide_ = false;
  std::vector<std::string> exceptions_;
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_COSMETIC_FILTER_HOST_H_
