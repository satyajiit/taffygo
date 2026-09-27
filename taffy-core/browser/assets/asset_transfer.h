// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_H_
#define TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_H_

#include <stdint.h>

#include <array>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/mojom/url_response_head.mojom-forward.h"
#include "url/gurl.h"

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace taffy {

class AssetTransferSink;

// How one transfer ended, in the vocabulary the Core Service contract uses.
//
// The transport members and the integrity members are separate because a
// truncated transfer and a wrong artifact are different accidents: one may be
// resumed and one may not, and the delivery plane's retry rule differs between
// them.
enum class AssetTransferOutcome {
  // The transfer stopped early; what is on disk may be resumed.
  kInterrupted,
  // The origin answered with something other than bytes; asking again may help.
  kOriginRefusedTemporary,
  // The origin answered with something other than bytes; asking again cannot.
  kOriginRefusedPermanent,
  // Every byte arrived and matched the catalog's length and digest.
  kIntegritySound,
  // The transfer ended with a different number of bytes than the catalog names.
  kIntegrityWrongLength,
  // The right number of bytes hashed to something else.
  kIntegrityWrongDigest,
  // Sound bytes were committed to their installed place.
  kInstalled,
};

// What one transfer observed. Facts only; what they mean is the core's.
struct AssetTransferReport {
  AssetTransferOutcome outcome = AssetTransferOutcome::kInterrupted;
  // Total bytes on disk, not the count this attempt moved.
  uint64_t written_bytes = 0;
  // What the whole staged file hashed to, or all zero when nothing was judged.
  std::array<uint8_t, 32> observed_digest = {};
};

// One transfer of one asset, resumable, verified, and cancellable.
//
// It writes and hashes on a blocking sequence and drives the loader on the
// sequence it was created on, so a slow disk applies back-pressure to the
// socket rather than blocking the browser's UI thread: the next chunk is only
// requested once the previous one is on disk. That is also why the digest is
// computed as the bytes arrive rather than by re-reading the file at the end —
// on a phone the file is large and the read is not free.
//
// Verifying and installing are the same operation as fetching, and end in one
// answer. A caller that heard "the bytes are sound" and then had to ask for
// them to be committed would be a caller that can forget, and a verified file
// left in staging is a file the next scan reports as partial.
//
// Resuming re-hashes the staged prefix before asking for more. A streaming hash
// has no serialisable state, so the alternative would be storing one, and a
// stored hash state is a second thing that can disagree with the file it
// describes. Reading the prefix once is sequential, happens on a blocking
// sequence, and cannot be wrong.
class AssetTransfer : public network::SimpleURLLoaderStreamConsumer {
 public:
  // What to fetch and what the answer must be.
  struct Request {
    // The absolute URL, already built from the pinned origin.
    GURL url;
    // The asset's identity and revision, for the staging file's name.
    std::string asset_id;
    std::string asset_revision;
    // The first byte wanted; zero for a fresh transfer.
    uint64_t offset_bytes = 0;
    // How many bytes the whole artifact has.
    uint64_t total_bytes = 0;
    // What the whole artifact must hash to.
    std::array<uint8_t, 32> expected_digest = {};
  };

  using ProgressCallback = base::RepeatingCallback<void(uint64_t written_bytes)>;
  using CompleteCallback = base::OnceCallback<void(AssetTransferReport)>;

  // `file_runner` must permit blocking. `store_root` is the profile's asset
  // directory, which the transfer writes into and nothing else does.
  AssetTransfer(scoped_refptr<network::SharedURLLoaderFactory> factory,
                scoped_refptr<base::SequencedTaskRunner> file_runner,
                base::FilePath store_root);
  AssetTransfer(const AssetTransfer&) = delete;
  AssetTransfer& operator=(const AssetTransfer&) = delete;
  ~AssetTransfer() override;

  // Begins. `on_complete` runs exactly once unless the transfer is destroyed.
  void Start(Request request,
             ProgressCallback on_progress,
             CompleteCallback on_complete);

  // network::SimpleURLLoaderStreamConsumer:
  void OnDataReceived(std::string_view chunk, base::OnceClosure resume) override;
  void OnComplete(bool success) override;
  void OnRetry(base::OnceClosure start_retry) override;

 private:
  void OnSinkOpened(bool opened);
  // Judges the response head, before a single body byte is accepted.
  //
  // This is the only place a status is read, and it runs even when the body is
  // empty. Judging on the first chunk instead would leave an empty `404` to be
  // discovered at the end as a file of the wrong length, which is a true
  // statement about the disk and the wrong thing to tell the delivery plane.
  void OnResponseStarted(const GURL& final_url,
                         const network::mojom::URLResponseHead& head);
  void OnChunkWritten(base::OnceClosure resume, uint64_t written_bytes);
  void OnDigest(std::array<uint8_t, 32> digest);
  void OnCommitted(bool committed);
  void Settle(AssetTransferOutcome outcome);

  const scoped_refptr<network::SharedURLLoaderFactory> factory_;
  const scoped_refptr<base::SequencedTaskRunner> file_runner_;
  const base::FilePath store_root_;

  Request request_;
  ProgressCallback on_progress_;
  CompleteCallback on_complete_;

  base::SequenceBound<AssetTransferSink> sink_;
  std::array<uint8_t, 32> observed_digest_ = {};
  std::unique_ptr<network::SimpleURLLoader> loader_;
  uint64_t written_bytes_ = 0;
  bool settled_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<AssetTransfer> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_ASSETS_ASSET_TRANSFER_H_
