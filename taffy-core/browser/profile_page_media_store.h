// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_PAGE_MEDIA_STORE_H_
#define TAFFY_BROWSER_PROFILE_PAGE_MEDIA_STORE_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace taffy {

// Profile-partitioned, bounded custody for browser-rendered exact-node pixels.
//
// Rust can hold the random handle but cannot resolve it. A model send claims
// the bytes once with the same task/generation, PAGE_CONTENT disclosure and
// MIME type. Every terminal mismatch burns the named handle, so a stale or
// confused caller cannot probe it repeatedly. Nothing here is durable or
// projected into status/audit.
class ProfilePageMediaStore : public base::RefCounted<ProfilePageMediaStore> {
 public:
  struct StoreRequest {
    std::string task_id;
    std::string observation_effect_id;
    uint64_t service_generation = 0u;
    std::string tab_id;
    std::string frame_id;
    std::string page_epoch;
    uint64_t graph_revision = 0u;
    std::optional<std::string> node_id;
    uint32_t width_px = 0u;
    uint32_t height_px = 0u;
  };

  struct StoredAttachment {
    std::string handle;
    std::string mime_type;
    uint32_t width_px = 0u;
    uint32_t height_px = 0u;
  };

  struct ResolvedAttachment {
    std::string observation_effect_id;
    std::string tab_id;
    std::string frame_id;
    std::string page_epoch;
    uint64_t graph_revision = 0u;
    std::optional<std::string> node_id;
    std::string mime_type;
    uint32_t width_px = 0u;
    uint32_t height_px = 0u;
    std::vector<uint8_t> bytes;
  };

  using DocumentBindingValidator =
      base::RepeatingCallback<bool(const StoreRequest&)>;

  explicit ProfilePageMediaStore(DocumentBindingValidator validator);
  ProfilePageMediaStore(const ProfilePageMediaStore&) = delete;
  ProfilePageMediaStore& operator=(const ProfilePageMediaStore&) = delete;

  // Stores PNG bytes produced by the browser compositor. The byte vector is
  // moved into custody and never copied on a successful path.
  std::optional<StoredAttachment> StoreRenderedPng(StoreRequest request,
                                                   std::vector<uint8_t> bytes,
                                                   base::TimeTicks now);

  // One-use claim for a model request. A known handle is erased before any
  // binding is checked; success returns its bytes by move.
  std::optional<ResolvedAttachment> TakeForModel(
      std::string_view task_id,
      uint64_t service_generation,
      std::string_view handle,
      std::string_view mime_type,
      core_service::mojom::DisclosureClass disclosure,
      base::TimeTicks now);

  void RevokeTask(std::string_view task_id, uint64_t service_generation);
  void RevokeDocument(std::string_view tab_id, std::string_view page_epoch);
  void RevokeGeneration(uint64_t service_generation);
  void RevokeAll();

  size_t entry_count_for_testing() const { return entries_.size(); }
  size_t total_bytes_for_testing() const { return total_bytes_; }

 private:
  friend class base::RefCounted<ProfilePageMediaStore>;
  ~ProfilePageMediaStore();

  struct Entry {
    StoreRequest binding;
    std::string mime_type;
    base::TimeTicks expires_at;
    std::vector<uint8_t> bytes;
  };

  void PruneExpired(base::TimeTicks now);
  void Erase(std::map<std::string, Entry>::iterator entry);

  std::map<std::string, Entry> entries_;
  size_t total_bytes_ = 0u;
  const DocumentBindingValidator document_binding_validator_;
};

// Binds a store to the exact live tab/frame/document/revision directory owned
// by one profile. Kept outside the store so its custody type does not retain a
// BrowserContext or gain any ability to navigate a page.
ProfilePageMediaStore::DocumentBindingValidator
CreateLivePageMediaDocumentValidator(content::BrowserContext* browser_context);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_PAGE_MEDIA_STORE_H_
