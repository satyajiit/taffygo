// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/bundled_asset_seeder.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

#include "base/files/memory_mapped_file.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"
#include "taffy/browser/assets/asset_store.h"
#include "taffy/browser/assets/asset_transfer_sink.h"

namespace taffy {

namespace {

// Written in pieces rather than in one call, so peak memory is a chunk and
// not an artifact. The largest row today is the Python standard library at
// 2.7 MB, and the seeder runs while a person is waiting for a first launch.
constexpr size_t kChunkBytes = 256u * 1024u;

std::string LowercaseHex(base::span<const uint8_t> digest) {
  return base::ToLowerASCII(base::HexEncode(digest));
}

}  // namespace

BundledSeedOutcome SeedBundledAssets(const base::FilePath& store_root,
                                     BundledAssetSource& source,
                                     base::span<const BundledAssetRow> rows) {
  BundledSeedOutcome outcome;
  if (store_root.empty()) {
    // A store with nowhere to put anything, which is what a build or a test
    // with no asset directory gets. `AssetStore` already refuses every write
    // in that state, and letting it would count each refusal as a package
    // whose bytes were wrong — a different and much more alarming thing than
    // having no store at all.
    return outcome;
  }
  AssetStore store(store_root);
  for (const BundledAssetRow& row : rows) {
    // The exact revision, not merely the identity. Two revisions coexist in
    // this store by design, and asking whether *something* is installed would
    // leave a device that upgraded holding the old artifact while the compiled
    // catalogue names the new one.
    if (store.OpenInstalled(row.asset_id, row.asset_revision).IsValid()) {
      ++outcome.already_installed;
      continue;
    }

    std::unique_ptr<base::MemoryMappedFile> mapped = source.Map(
        row.apk_asset_path);
    if (!mapped) {
      ++outcome.absent;
      continue;
    }

    base::span<const uint8_t> bytes = mapped->bytes();
    std::array<uint8_t, 32> digest = {};
    crypto::hash::Hash(crypto::hash::kSha256, bytes, digest);
    if (bytes.size() != row.transfer_bytes ||
        LowercaseHex(digest) != row.digest) {
      ++outcome.refused;
      continue;
    }

    // The same sink the network half writes through, because a seed is a
    // transfer whose bytes came from the package: staging file, hash, atomic
    // commit. Reusing it is what keeps a seeded artifact and a fetched one
    // indistinguishable on disk, which is what lets every reader above stay
    // unaware that there are two ways in.
    bool committed = false;
    {
      AssetTransferSink sink(store_root);
      if (!sink.Open(std::string(row.asset_id), std::string(row.asset_revision),
                     0u)) {
        ++outcome.refused;
        continue;
      }
      bool wrote = true;
      for (size_t offset = 0; offset < bytes.size(); offset += kChunkBytes) {
        const size_t length = std::min(kChunkBytes, bytes.size() - offset);
        base::span<const uint8_t> chunk = bytes.subspan(offset, length);
        if (!sink.Write(std::string(reinterpret_cast<const char*>(chunk.data()),
                                    chunk.size()))) {
          wrote = false;
          break;
        }
      }
      // Finished whatever happened, because it is what flushes. Its answer is
      // not consulted: the digest that decides was taken from the mapping
      // above, and a second opinion taken from the copy could only disagree
      // if the disk lied about what it had just been given.
      sink.Finish();
      committed = wrote && sink.Commit();
    }
    // The sink is destroyed — and its descriptor closed — before anything is
    // deleted. Removing an open file succeeds on POSIX and fails on Windows,
    // and the sink beside this one says in as many words that the plane must
    // behave the same on both.
    if (!committed) {
      store.Remove(row.asset_id, row.asset_revision);
      ++outcome.refused;
      continue;
    }
    ++outcome.seeded;
    outcome.seeded_ids.emplace_back(row.asset_id);
  }

  // Said once per pass, and only when the pass did something. A device where
  // every artifact was already installed is the ordinary second launch and
  // has nothing to report; a device where all four were refused or absent
  // looks exactly the same from every surface above — the start page waits,
  // the flags are placeholders, the ads are blocked by the fallback text —
  // and without this line there is nothing on the phone that says why.
  if (outcome.seeded > 0 || outcome.absent > 0 || outcome.refused > 0) {
    LOG(WARNING) << "[taffy_bundled_assets] seeded=" << outcome.seeded
                 << " already=" << outcome.already_installed
                 << " absent=" << outcome.absent
                 << " refused=" << outcome.refused;
  }
  return outcome;
}

}  // namespace taffy
