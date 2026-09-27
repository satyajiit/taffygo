// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/filtering_list_reader.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "taffy/browser/assets/asset_pack.h"
#include "taffy/browser/assets/profile_asset_plane.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/apk_assets.h"
#include "base/files/file.h"
#include "base/files/memory_mapped_file.h"
#endif

namespace taffy {

namespace {

// The catalog row this reader is about, and its pack's members in the order
// they concatenate. The names are the build recipe's
// (taffy-core/third_party/easylist/tools/manifest.json); the pack digest was
// verified at install, so agreement here is a naming fact, not a trust one.
constexpr char kFilterListAssetId[] = "easylist-base";
constexpr std::array<std::string_view, 2> kListMembers = {
    "lists/easylist.txt",
    "lists/easyprivacy.txt",
};

// Generous bounds: the member path is short, and a base list runs a few
// megabytes. A member past the bound is refused by the pack reader, and the
// whole load answers nullopt rather than compiling half a rule set.
constexpr size_t kMaxMemberPathBytes = 64;
constexpr size_t kMaxListMemberBytes = 32 * 1024 * 1024;

// The walk carries the plane weakly, because it outlives a single call.
//
// Reading two members is two round trips to the asset plane's file runner, and
// a profile can be destroyed between them — its `ProfileAssetPlane` with it.
// A bare pointer through this walk is a use-after-free on the second leg, and
// on Android it is the reply that runs first: PartitionAlloc reported it as a
// dangling `raw_ptr` in unretained, fatal, from
// `CoreServiceManagerFactoryBrowserTest`, which is a test that creates and
// destroys profiles. A plane that is gone answers the same way an unreadable
// member does — no lists — and the caller falls back to the bundled snapshot.
void ReadNextMember(base::WeakPtr<ProfileAssetPlane> plane,
                    size_t index,
                    std::string accumulated,
                    base::OnceCallback<void(std::optional<std::string>)> reply);

void OnMemberRead(base::WeakPtr<ProfileAssetPlane> plane,
                  size_t index,
                  std::string accumulated,
                  base::OnceCallback<void(std::optional<std::string>)> reply,
                  PackMemberResult result) {
  if (result.verdict != PackMemberVerdict::kOk) {
    std::move(reply).Run(std::nullopt);
    return;
  }
  accumulated.append(result.bytes.begin(), result.bytes.end());
  accumulated.push_back('\n');
  ReadNextMember(std::move(plane), index + 1, std::move(accumulated),
                 std::move(reply));
}

void ReadNextMember(
    base::WeakPtr<ProfileAssetPlane> plane,
    size_t index,
    std::string accumulated,
    base::OnceCallback<void(std::optional<std::string>)> reply) {
  if (index >= kListMembers.size()) {
    std::move(reply).Run(std::move(accumulated));
    return;
  }
  if (!plane) {
    std::move(reply).Run(std::nullopt);
    return;
  }
  ProfileAssetPlane* const live = plane.get();
  live->ReadMember(kFilterListAssetId, std::string(kListMembers[index]),
                   kMaxMemberPathBytes, kMaxListMemberBytes,
                   base::BindOnce(&OnMemberRead, std::move(plane), index,
                                  std::move(accumulated), std::move(reply)));
}

// MemoryMappedFile::Initialize is a blocking call. The catalog path already
// reads on the asset plane's MayBlock runner; the APK fallback must too,
// because this reader is invoked from the UI sequence.
#if BUILDFLAG(IS_ANDROID)
std::optional<std::string> ReadBundledFilterListsBlocking() {
  // Uncompressed APK asset of the pinned EasyList+EasyPrivacy snapshots.
  // Path form matches OpenApkAsset ("assets/icudtl.dat").
  constexpr char kBundledAssetPath[] = "assets/taffy-filter-lists/base.txt";
  base::MemoryMappedFile::Region region;
  const int fd = base::android::OpenApkAsset(kBundledAssetPath, &region);
  if (fd < 0) {
    return std::nullopt;
  }
  base::MemoryMappedFile mapped;
  if (!mapped.Initialize(base::File(fd), region) || mapped.length() == 0) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char*>(mapped.data()),
                     mapped.length());
}
#endif

void ReadBundledFilterLists(
    base::OnceCallback<void(std::optional<std::string>)> reply) {
#if BUILDFLAG(IS_ANDROID)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadBundledFilterListsBlocking), std::move(reply));
#else
  std::move(reply).Run(std::nullopt);
#endif
}

void OnCatalogLists(base::OnceCallback<void(std::optional<std::string>)> reply,
                    std::optional<std::string> lists) {
  if (lists.has_value()) {
    std::move(reply).Run(std::move(lists));
    return;
  }
  ReadBundledFilterLists(std::move(reply));
}

}  // namespace

filtering::FilteringRulesetService::ListReader MakeFilterListReader(
    ProfileAssetPlane* asset_plane) {
  // Weak from the moment the reader is made: this is a repeating callback the
  // ruleset service keeps, and nothing promises the plane outlives it.
  return base::BindRepeating(
      [](base::WeakPtr<ProfileAssetPlane> plane,
         base::OnceCallback<void(std::optional<std::string>)> reply) {
        ReadNextMember(std::move(plane), 0, std::string(),
                       base::BindOnce(&OnCatalogLists, std::move(reply)));
      },
      asset_plane->GetWeakPtr());
}

}  // namespace taffy
