// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_page_media_store.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

constexpr size_t kMaximumEntries = 8u;
constexpr size_t kMaximumProfileBytes = 16u * 1024u * 1024u;
constexpr base::TimeDelta kAttachmentLifetime = base::Minutes(2);
constexpr std::array<uint8_t, 8> kPngSignature = {0x89u, 0x50u, 0x4eu, 0x47u,
                                                  0x0du, 0x0au, 0x1au, 0x0au};
constexpr size_t kPngHeaderBytes = 33u;

bool BoundedIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= service_mojom::kMaxIdentifierBytes;
}

uint32_t ReadBigEndianUint32(base::span<const uint8_t> bytes, size_t offset) {
  return (static_cast<uint32_t>(bytes[offset]) << 24u) |
         (static_cast<uint32_t>(bytes[offset + 1u]) << 16u) |
         (static_cast<uint32_t>(bytes[offset + 2u]) << 8u) |
         static_cast<uint32_t>(bytes[offset + 3u]);
}

bool IsBrowserPng(base::span<const uint8_t> bytes,
                  uint32_t expected_width,
                  uint32_t expected_height) {
  return bytes.size() >= kPngHeaderBytes &&
         std::ranges::equal(kPngSignature, bytes.first(kPngSignature.size())) &&
         ReadBigEndianUint32(bytes, 8u) == 13u && bytes[12u] == 'I' &&
         bytes[13u] == 'H' && bytes[14u] == 'D' && bytes[15u] == 'R' &&
         ReadBigEndianUint32(bytes, 16u) == expected_width &&
         ReadBigEndianUint32(bytes, 20u) == expected_height;
}

}  // namespace

ProfilePageMediaStore::ProfilePageMediaStore(DocumentBindingValidator validator)
    : document_binding_validator_(std::move(validator)) {
  CHECK(document_binding_validator_);
}

ProfilePageMediaStore::~ProfilePageMediaStore() = default;

std::optional<ProfilePageMediaStore::StoredAttachment>
ProfilePageMediaStore::StoreRenderedPng(StoreRequest request,
                                        std::vector<uint8_t> bytes,
                                        base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PruneExpired(now);
  if (!BoundedIdentifier(request.task_id) ||
      !BoundedIdentifier(request.observation_effect_id) ||
      request.service_generation == 0u || !BoundedIdentifier(request.tab_id) ||
      !BoundedIdentifier(request.frame_id) ||
      !BoundedIdentifier(request.page_epoch) || request.graph_revision == 0u ||
      (request.node_id.has_value() && !BoundedIdentifier(*request.node_id)) ||
      request.width_px == 0u || request.height_px == 0u ||
      request.width_px > service_mojom::kMaxMediaDimensionPx ||
      request.height_px > service_mojom::kMaxMediaDimensionPx ||
      bytes.empty() ||
      bytes.size() > service_mojom::kMaxMediaAttachmentBytes ||
      !IsBrowserPng(bytes, request.width_px, request.height_px) ||
      entries_.size() >= kMaximumEntries ||
      total_bytes_ > kMaximumProfileBytes ||
      bytes.size() > kMaximumProfileBytes - total_bytes_) {
    return std::nullopt;
  }

  std::string handle;
  do {
    handle = "media-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  } while (entries_.contains(handle));
  if (handle.size() > service_mojom::kMaxMediaAttachmentHandleBytes) {
    return std::nullopt;
  }
  const size_t stored_bytes = bytes.size();
  Entry entry{.binding = std::move(request),
              .mime_type = "image/png",
              .expires_at = now + kAttachmentLifetime,
              .bytes = std::move(bytes)};
  const uint32_t width = entry.binding.width_px;
  const uint32_t height = entry.binding.height_px;
  auto [inserted, unique] = entries_.emplace(handle, std::move(entry));
  if (!unique) {
    return std::nullopt;
  }
  total_bytes_ += stored_bytes;
  return StoredAttachment{.handle = std::move(handle),
                          .mime_type = inserted->second.mime_type,
                          .width_px = width,
                          .height_px = height};
}

std::optional<ProfilePageMediaStore::ResolvedAttachment>
ProfilePageMediaStore::TakeForModel(std::string_view task_id,
                                    uint64_t service_generation,
                                    std::string_view handle,
                                    std::string_view mime_type,
                                    service_mojom::DisclosureClass disclosure,
                                    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PruneExpired(now);
  if (handle.empty() ||
      handle.size() > service_mojom::kMaxMediaAttachmentHandleBytes) {
    return std::nullopt;
  }
  auto found = entries_.find(std::string(handle));
  if (found == entries_.end()) {
    return std::nullopt;
  }
  Entry entry = std::move(found->second);
  total_bytes_ -= entry.bytes.size();
  entries_.erase(found);
  if (entry.binding.task_id != task_id ||
      entry.binding.service_generation != service_generation ||
      entry.mime_type != mime_type ||
      disclosure != service_mojom::DisclosureClass::kPageContent ||
      entry.expires_at <= now ||
      !document_binding_validator_.Run(entry.binding)) {
    return std::nullopt;
  }
  return ResolvedAttachment{
      .observation_effect_id = std::move(entry.binding.observation_effect_id),
      .tab_id = std::move(entry.binding.tab_id),
      .frame_id = std::move(entry.binding.frame_id),
      .page_epoch = std::move(entry.binding.page_epoch),
      .graph_revision = entry.binding.graph_revision,
      .node_id = std::move(entry.binding.node_id),
      .mime_type = std::move(entry.mime_type),
      .width_px = entry.binding.width_px,
      .height_px = entry.binding.height_px,
      .bytes = std::move(entry.bytes),
  };
}

void ProfilePageMediaStore::RevokeTask(std::string_view task_id,
                                       uint64_t service_generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.binding.task_id == task_id &&
        it->second.binding.service_generation == service_generation) {
      Erase(it++);
    } else {
      ++it;
    }
  }
}

void ProfilePageMediaStore::RevokeDocument(std::string_view tab_id,
                                           std::string_view page_epoch) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.binding.tab_id == tab_id &&
        it->second.binding.page_epoch == page_epoch) {
      Erase(it++);
    } else {
      ++it;
    }
  }
}

void ProfilePageMediaStore::RevokeGeneration(uint64_t service_generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.binding.service_generation == service_generation) {
      Erase(it++);
    } else {
      ++it;
    }
  }
}

void ProfilePageMediaStore::RevokeAll() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  entries_.clear();
  total_bytes_ = 0u;
}

void ProfilePageMediaStore::PruneExpired(base::TimeTicks now) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->second.expires_at <= now) {
      Erase(it++);
    } else {
      ++it;
    }
  }
}

void ProfilePageMediaStore::Erase(
    std::map<std::string, Entry>::iterator entry) {
  total_bytes_ -= entry->second.bytes.size();
  entries_.erase(entry);
}

ProfilePageMediaStore::DocumentBindingValidator
CreateLivePageMediaDocumentValidator(content::BrowserContext* browser_context) {
  return base::BindRepeating(
      [](content::BrowserContext* context,
         const ProfilePageMediaStore::StoreRequest& binding) {
        const std::optional<TaskPolicyDocumentContext> live =
            ResolveTaskPolicyDocument(context, binding.tab_id);
        return live && live->frame_id == binding.frame_id &&
               live->page_epoch == binding.page_epoch &&
               live->graph_revision == binding.graph_revision;
      },
      browser_context);
}

}  // namespace taffy
