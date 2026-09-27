// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/profile_asset_plane.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "taffy/browser/asset_delivery_configuration.h"
#include "taffy/browser/assets/bundled_asset_seeder.h"
#include "taffy/browser/assets/bundled_asset_source.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

service::AssetTransferOutcome ToWire(AssetTransferOutcome outcome) {
  switch (outcome) {
    case AssetTransferOutcome::kInterrupted:
      return service::AssetTransferOutcome::kInterrupted;
    case AssetTransferOutcome::kOriginRefusedTemporary:
      return service::AssetTransferOutcome::kOriginRefusedTemporary;
    case AssetTransferOutcome::kOriginRefusedPermanent:
      return service::AssetTransferOutcome::kOriginRefusedPermanent;
    case AssetTransferOutcome::kIntegritySound:
      return service::AssetTransferOutcome::kIntegritySound;
    case AssetTransferOutcome::kIntegrityWrongLength:
      return service::AssetTransferOutcome::kIntegrityWrongLength;
    case AssetTransferOutcome::kIntegrityWrongDigest:
      return service::AssetTransferOutcome::kIntegrityWrongDigest;
    case AssetTransferOutcome::kInstalled:
      return service::AssetTransferOutcome::kInstalled;
  }
}

service::AssetDeliveryEffectResultPtr TransferResult(
    const std::string& asset_id,
    const std::string& asset_revision,
    service::AssetTransferOutcome outcome,
    uint64_t written_bytes,
    const std::array<uint8_t, 32>& digest) {
  auto report = service::AssetTransferReport::New();
  report->asset_id = asset_id;
  report->asset_revision = asset_revision;
  report->outcome = outcome;
  report->written_bytes = written_bytes;
  report->observed_bytes = written_bytes;
  report->observed_digest = std::vector<uint8_t>(digest.begin(), digest.end());

  auto result = service::AssetDeliveryEffectResult::New();
  result->operation_kind = service::AssetDeliveryOperation::kFetchAsset;
  result->transfer = std::move(report);
  return result;
}

}  // namespace

ProfileAssetPlane::ProfileAssetPlane(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    base::FilePath store_root,
    std::string origin)
    : factory_(std::move(factory)),
      store_root_(std::move(store_root)),
      origin_(std::move(origin)),
      file_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

ProfileAssetPlane::~ProfileAssetPlane() = default;

void ProfileAssetPlane::SeedBundledPartsAndScan(ScanCallback callback) {
  SeedAndScan(std::nullopt,
              {kBundledAssetRows.begin(), kBundledAssetRows.end()},
              std::move(callback));
}

void ProfileAssetPlane::SeedBundledPartsAndScanForTesting(
    base::FilePath package_root,
    std::vector<BundledAssetRow> rows,
    ScanCallback callback) {
  SeedAndScan(std::move(package_root), std::move(rows), std::move(callback));
}

void ProfileAssetPlane::SeedAndScan(
    std::optional<base::FilePath> package_root,
    std::vector<BundledAssetRow> rows,
    ScanCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath root, std::optional<base::FilePath> package_root,
             std::vector<BundledAssetRow> rows) {
            // The source is built here rather than passed in, because it is
            // used on this sequence and nowhere else and a handle that crossed
            // the boundary would be one more lifetime to reason about.
            std::unique_ptr<BundledAssetSource> source;
            if (package_root.has_value()) {
              source = std::make_unique<DirectoryBundledAssetSource>(
                  std::move(package_root).value());
            } else {
              source = std::make_unique<ApkBundledAssetSource>();
            }
            // Both halves on one task, so the scan cannot read the store
            // before the package has finished putting things into it.
            SeededScan seeded;
            seeded.outcome = SeedBundledAssets(root, *source, rows);
            seeded.rows = AssetStore(std::move(root)).Scan();
            return seeded;
          },
          store_root_, std::move(package_root), std::move(rows)),
      base::BindOnce(&ProfileAssetPlane::OnSeededAndScanned,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void ProfileAssetPlane::OnSeededAndScanned(ScanCallback callback,
                                           SeededScan seeded) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Announced before the scan is answered, and for a reason that is about
  // order rather than tidiness. Answering the scan launches the core, and the
  // core's plan is a pure function of what the scan said; the readers above
  // this plane are a different audience, and they were built against an empty
  // store before any of this ran. Telling them first means no window exists in
  // which the core believes an artifact is installed and the browser's own
  // readers do not.
  for (const std::string& asset_id : seeded.outcome.seeded_ids) {
    if (installed_observer_) {
      installed_observer_.Run(asset_id);
    }
  }
  OnScanned(std::move(callback), std::move(seeded.rows));
}

void ProfileAssetPlane::OnScanned(ScanCallback callback,
                                  std::vector<AssetStore::Found> rows) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<service::AssetOnDiskPtr> found;
  found.reserve(rows.size());
  for (const AssetStore::Found& row : rows) {
    auto record = service::AssetOnDisk::New();
    record->asset_id = row.asset_id;
    record->asset_revision = row.asset_revision;
    // Installed wins over staged. Both can exist at once — a resumed transfer
    // of a revision that is already installed — and the finished artifact is
    // what the device can use.
    record->presence = row.installed ? service::AssetPresence::kInstalled
                                     : service::AssetPresence::kPartial;
    record->written_bytes =
        row.installed ? row.installed_bytes : row.staged_bytes;
    found.push_back(std::move(record));
  }
  std::move(callback).Run(std::move(found));
}

void ProfileAssetPlane::Dispatch(
    service::EffectEnvelopePtr effect,
    base::OnceCallback<void(service::EffectResultPtr)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->asset_delivery) {
    std::move(callback).Run(nullptr);
    return;
  }
  auto result = service::EffectResult::New();
  result->operation = effect->operation.Clone();
  result->effect_id = effect->effect_id;
  result->kind = service::EffectKind::kDeliverAsset;
  Perform(std::move(effect->asset_delivery),
          base::BindOnce(
              [](service::EffectResultPtr result,
                 base::OnceCallback<void(service::EffectResultPtr)> callback,
                 service::AssetDeliveryEffectResultPtr delivery) {
                if (!delivery) {
                  // A body the plane could not act on at all. The broker's own
                  // terminal is the right answer, and it makes one from the
                  // envelope it still holds.
                  std::move(callback).Run(nullptr);
                  return;
                }
                // Every outcome the plane reports is a fact it observed, so the
                // effect completed even when the artifact did not arrive. What
                // the outcome means is the core's to decide.
                result->status = service::EffectStatus::kCompleted;
                result->asset_delivery = std::move(delivery);
                std::move(callback).Run(std::move(result));
              },
              std::move(result), std::move(callback)));
}

void ProfileAssetPlane::Perform(service::AssetDeliveryEffectPtr effect,
                                PerformCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect) {
    std::move(callback).Run(nullptr);
    return;
  }

  if (effect->operation_kind == service::AssetDeliveryOperation::kRemoveAsset) {
    if (!effect->remove) {
      std::move(callback).Run(nullptr);
      return;
    }
    const std::string asset_id = effect->remove->asset_id;
    const std::string asset_revision = effect->remove->asset_revision;
    file_runner_->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            [](base::FilePath root, std::string id, std::string revision) {
              return AssetStore(std::move(root)).Remove(id, revision);
            },
            store_root_, asset_id, asset_revision),
        base::BindOnce(&ProfileAssetPlane::OnRemoved,
                       weak_factory_.GetWeakPtr(), asset_id, asset_revision,
                       std::move(callback)));
    return;
  }

  if (!effect->fetch) {
    std::move(callback).Run(nullptr);
    return;
  }
  service::AssetFetchRequestPtr fetch = std::move(effect->fetch);

  if (transfer_) {
    // The core plans one transfer at a time. A second arriving is a defect
    // upstream rather than a queue to grow here, and answering "interrupted"
    // makes it retryable instead of silently dropping the effect.
    RefuseFetch(std::move(fetch), std::move(callback),
                service::AssetTransferOutcome::kInterrupted);
    return;
  }

  const GURL url = AssetUrl(origin_, fetch->origin_path);
  if (!url.is_valid()) {
    // No delivery origin is configured, or the path is not one the catalog
    // could have written. Neither is fixed by waiting.
    RefuseFetch(std::move(fetch), std::move(callback),
                service::AssetTransferOutcome::kOriginRefusedPermanent);
    return;
  }
  if (fetch->expected_digest.size() != 32u) {
    RefuseFetch(std::move(fetch), std::move(callback),
                service::AssetTransferOutcome::kOriginRefusedPermanent);
    return;
  }

  AssetTransfer::Request request;
  request.url = url;
  request.asset_id = fetch->asset_id;
  request.asset_revision = fetch->asset_revision;
  request.offset_bytes = fetch->offset_bytes;
  request.total_bytes = fetch->total_bytes;
  std::ranges::copy(fetch->expected_digest, request.expected_digest.begin());

  live_ = LiveProgress{fetch->asset_id, fetch->asset_revision,
                       fetch->offset_bytes, fetch->total_bytes};

  transfer_ = std::make_unique<AssetTransfer>(factory_, file_runner_,
                                              store_root_);
  transfer_->Start(
      std::move(request),
      base::BindRepeating(&ProfileAssetPlane::OnTransferProgress,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&ProfileAssetPlane::OnTransferComplete,
                     weak_factory_.GetWeakPtr(), fetch->asset_id,
                     fetch->asset_revision, std::move(callback)));
}

void ProfileAssetPlane::RefuseFetch(service::AssetFetchRequestPtr fetch,
                                    PerformCallback callback,
                                    service::AssetTransferOutcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(TransferResult(fetch->asset_id, fetch->asset_revision,
                                         outcome, fetch->offset_bytes,
                                         std::array<uint8_t, 32>{}));
}

void ProfileAssetPlane::SetProgressObserver(
    base::RepeatingCallback<void(const LiveProgress&)> observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  progress_observer_ = std::move(observer);
}

void ProfileAssetPlane::SetInstalledObserver(
    base::RepeatingCallback<void(std::string_view)> observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  installed_observer_ = std::move(observer);
}

void ProfileAssetPlane::OnTransferProgress(uint64_t written_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  live_.written_bytes = written_bytes;
  if (progress_observer_) {
    progress_observer_.Run(live_);
  }
}

void ProfileAssetPlane::OnTransferComplete(std::string asset_id,
                                           std::string asset_revision,
                                           PerformCallback callback,
                                           AssetTransferReport report) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  transfer_.reset();
  live_ = LiveProgress{};
  // Only `kInstalled` moved anything into place. A refused, interrupted or
  // integrity-mismatched transfer leaves the staged file exactly where it was,
  // in the directory the installed set is not read from. Notified before the
  // result travels to the core, because this is a fact about this process's
  // own disk and is settled the moment the commit returned; what the core
  // makes of the report is a separate question and a later one.
  if (report.outcome == AssetTransferOutcome::kInstalled &&
      installed_observer_) {
    installed_observer_.Run(asset_id);
  }
  std::move(callback).Run(TransferResult(asset_id, asset_revision,
                                         ToWire(report.outcome),
                                         report.written_bytes,
                                         report.observed_digest));
}

void ProfileAssetPlane::OnRemoved(std::string asset_id,
                                  std::string asset_revision,
                                  PerformCallback callback,
                                  uint64_t reclaimed_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Unconditionally, including a removal that freed nothing. Reading the byte
  // count as "the installed set did not change" would put a second rule about
  // what the disk holds in this class, where it could disagree with the disk;
  // an observer that answers by looking cannot.
  if (installed_observer_) {
    installed_observer_.Run(asset_id);
  }
  auto removal = service::AssetRemovalReport::New();
  removal->asset_id = std::move(asset_id);
  removal->asset_revision = std::move(asset_revision);
  removal->reclaimed_bytes = reclaimed_bytes;

  auto result = service::AssetDeliveryEffectResult::New();
  result->operation_kind = service::AssetDeliveryOperation::kRemoveAsset;
  result->removal = std::move(removal);
  std::move(callback).Run(std::move(result));
}

std::vector<ProfileAssetPlane::LiveProgress> ProfileAssetPlane::live_progress()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!transfer_) {
    return {};
  }
  return {live_};
}

void ProfileAssetPlane::ReadMember(
    std::string asset_id,
    std::string member_path,
    size_t max_path_bytes,
    size_t max_bytes,
    base::OnceCallback<void(PackMemberResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath root, std::string id, std::string member,
             size_t path_bytes, size_t bytes) {
            const AssetStore store(std::move(root));
            return ReadPackMember(store, id, member, path_bytes, bytes);
          },
          store_root_, std::move(asset_id), std::move(member_path),
          max_path_bytes, max_bytes),
      std::move(callback));
}

void ProfileAssetPlane::OpenInstalled(
    std::string asset_id,
    std::string asset_revision,
    base::OnceCallback<void(base::File)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath root, std::string id, std::string revision) {
            return AssetStore(std::move(root)).OpenInstalled(id, revision);
          },
          store_root_, std::move(asset_id), std::move(asset_revision)),
      std::move(callback));
}

}  // namespace taffy
