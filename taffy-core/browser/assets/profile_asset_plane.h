// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_PROFILE_ASSET_PLANE_H_
#define TAFFY_BROWSER_ASSETS_PROFILE_ASSET_PLANE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file.h"
#include "taffy/browser/assets/asset_pack.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "taffy/browser/assets/asset_store.h"
#include "taffy/browser/assets/asset_transfer.h"
#include "taffy/browser/assets/bundled_asset_seeder.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// The browser half of the delivery plane, for one profile.
//
// It decides nothing. The isolated core answers what should be fetched, this
// class fetches it, and what comes back is a fact rather than a judgement —
// `core_service::mojom::AssetTransferReport` carries what was written and what
// it hashed to, and the core reads the meaning. Even the digest comparison
// that ends a transfer is a comparison against a number the core sent, not one
// this process chose.
//
// Two things live here that the core deliberately does not have:
//
//  * **The pinned origin.** A path travels in a message; a host never does. So
//    nothing a message carries can move a device to a different origin, and
//    the origin cannot be wrong in a way a message could cause.
//  * **Live byte progress.** A person watching a progress bar needs a number
//    that moves several times a second, and the core needs to know how a
//    transfer *ended*. Sending every tick across a process boundary to update
//    a number nothing in the core decides anything with would be two hops for
//    no decision, so the surface reads progress from here.
class ProfileAssetPlane {
 public:
  // What is happening to one asset right now, for the surface to render.
  struct LiveProgress {
    std::string asset_id;
    std::string asset_revision;
    uint64_t written_bytes = 0;
    uint64_t total_bytes = 0;
  };

  using PerformCallback = base::OnceCallback<void(
      core_service::mojom::AssetDeliveryEffectResultPtr)>;
  using ScanCallback = base::OnceCallback<void(
      std::vector<core_service::mojom::AssetOnDiskPtr>)>;

  // `store_root` is the profile-scoped directory this plane owns entirely.
  // `origin` is the compiled-in delivery origin, which may be empty — an
  // absent origin refuses every fetch and breaks nothing else.
  ProfileAssetPlane(scoped_refptr<network::SharedURLLoaderFactory> factory,
                    base::FilePath store_root,
                    std::string origin);
  ProfileAssetPlane(const ProfileAssetPlane&) = delete;
  ProfileAssetPlane& operator=(const ProfileAssetPlane&) = delete;
  ~ProfileAssetPlane();

  // Installs what the package carries, then answers what this profile has.
  //
  // The disk is the truth at start-up, not a record the core kept: a record
  // survives a crash and a file does not, and the one that decides whether a
  // transfer can resume is the file.
  //
  // The seeding is inside this call rather than beside it because the order is
  // the whole point and a second entry point is a second chance to get it
  // wrong. Decision 0202 ships the required artifacts in the package, so on a
  // first run they are installed here; an artifact seeded *after* the scan
  // would be invisible to the core for the life of that generation, which
  // would plan a download for it against an origin that is empty. One posted
  // task does both, in that order, and nothing can interleave.
  //
  // A package that does not carry an artifact, and an artifact whose bytes are
  // not what the compiled catalogue pins, both leave the store untouched and
  // neither fails the launch. See `SeedBundledAssets`.
  //
  // Anything this installs is announced to the installed-set observer before
  // the scan is answered, for the reason `BundledSeedOutcome::seeded_ids`
  // gives: the readers above this plane were constructed with an empty store
  // and cache that answer.
  void SeedBundledPartsAndScan(ScanCallback callback);

  // The same call, from a directory standing in for the package and with rows
  // of the caller's choosing.
  //
  // Everything after where the bytes come from is identical — the same digest
  // gate, the same store, the same commit, the same announcement before the
  // same scan — which is the point: a browser test that drove a second,
  // simpler path would prove that path and not this one. It exists because the
  // compiled rows name the four artifacts the real package carries, and a test
  // that needs a filter pack with one known rule in it cannot use those.
  //
  // The rows hold views. They must outlive the call, which is what waiting on
  // `callback` gives a caller.
  void SeedBundledPartsAndScanForTesting(base::FilePath package_root,
                                         std::vector<BundledAssetRow> rows,
                                         ScanCallback callback);

  // Performs one delivery effect. `callback` runs exactly once.
  void Perform(core_service::mojom::AssetDeliveryEffectPtr effect,
               PerformCallback callback);

  // The broker-shaped entry point: an envelope in, a terminal result out.
  //
  // It exists beside `Perform` rather than replacing it because the two have
  // different callers. The broker speaks envelopes and owns the journal
  // ordering around them; a test and a future register want the delivery body
  // alone, without minting an envelope to ask a question with.
  void Dispatch(core_service::mojom::EffectEnvelopePtr effect,
                base::OnceCallback<void(core_service::mojom::EffectResultPtr)>
                    callback);

  // Every transfer that is running, for the surface.
  std::vector<LiveProgress> live_progress() const;

  // Called as bytes land, for a surface that is drawing a progress bar.
  //
  // A push rather than a poll because the number moves several times a second
  // and a surface that asked for it on a timer would either ask too often or
  // draw a stale figure. It is deliberately not routed through the isolated
  // core: the core takes no decision from this, and a round trip per chunk
  // would put thousands of messages on a queue that carries decisions.
  void SetProgressObserver(
      base::RepeatingCallback<void(const LiveProgress&)> observer);

  // Called when the set of installed artifacts changes: a transfer that ended
  // in an install, or a removal. It carries only the stable asset identity so
  // an unrelated reader does not withdraw and rehash its own descriptor. It
  // is still a reason to look rather than a description of disk state; each
  // interested reader reopens the disk it owns.
  //
  // A push rather than a poll, for the opposite reason to the progress
  // observer above. Progress moves several times a second and could not be
  // polled cheaply; this moves a handful of times in a session, so a poller
  // would spend a directory walk on a timer to notice it.
  void SetInstalledObserver(
      base::RepeatingCallback<void(std::string_view)> observer);

  // Reads one member out of an installed packed artifact.
  //
  // Bounded by the two limits the caller passes, which are the contract's, so
  // this class never decides how much a surface may ask for. Whichever revision
  // is installed is the one read: a caller wants what is on the device.
  void ReadMember(std::string asset_id,
                  std::string member_path,
                  size_t max_path_bytes,
                  size_t max_bytes,
                  base::OnceCallback<void(PackMemberResult)> callback);

  // Opens an installed artifact read-only, for a register to hand a worker.
  // An invalid file means it is not installed, which is not an error.
  void OpenInstalled(std::string asset_id,
                     std::string asset_revision,
                     base::OnceCallback<void(base::File)> callback);

  // Whether a transfer is running. One at a time, and the core plans for that.
  bool has_transfer() const { return transfer_ != nullptr; }

  // For a walk that outlives one call and must stop if this plane does.
  //
  // The plane is owned by the profile's core-service manager, and a read it
  // started can still be in flight on the file runner when the profile is torn
  // down. A caller that holds it across that boundary holds it weakly, or the
  // reply runs against freed memory — which is what
  // `UnretainedDanglingRawPtrDetectedCrash` reported when the filter-list
  // reader carried a bare pointer through its member walk.
  base::WeakPtr<ProfileAssetPlane> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  // What one seed-then-scan task produced. The two travel together because one
  // task produces both and the reply needs both: the rows answer the core's
  // bootstrap, and the identities are what the readers above have to be told.
  struct SeededScan {
    BundledSeedOutcome outcome;
    std::vector<AssetStore::Found> rows;
  };

  // `package_root` absent means the package this binary was installed from.
  void SeedAndScan(std::optional<base::FilePath> package_root,
                   std::vector<BundledAssetRow> rows,
                   ScanCallback callback);
  void OnSeededAndScanned(ScanCallback callback, SeededScan seeded);
  void OnScanned(ScanCallback callback, std::vector<AssetStore::Found> rows);
  void OnTransferProgress(uint64_t written_bytes);
  void OnTransferComplete(std::string asset_id,
                          std::string asset_revision,
                          PerformCallback callback,
                          AssetTransferReport report);
  void OnRemoved(std::string asset_id,
                 std::string asset_revision,
                 PerformCallback callback,
                 uint64_t reclaimed_bytes);
  void RefuseFetch(core_service::mojom::AssetFetchRequestPtr fetch,
                   PerformCallback callback,
                   core_service::mojom::AssetTransferOutcome outcome);

  const scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const base::FilePath store_root_;
  const std::string origin_;
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;

  std::unique_ptr<AssetTransfer> transfer_;
  LiveProgress live_;
  base::RepeatingCallback<void(const LiveProgress&)> progress_observer_;
  base::RepeatingCallback<void(std::string_view)> installed_observer_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileAssetPlane> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_PROFILE_ASSET_PLANE_H_
