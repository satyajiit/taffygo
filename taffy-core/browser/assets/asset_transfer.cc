// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_transfer.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/assets/asset_transfer_sink.h"

namespace taffy {
namespace {

// The one traffic annotation for every asset fetch. It is one annotation and
// not one per asset because every fetch is the same request with a different
// path: no cookies, no credentials, no page content, and a URL the product
// chose rather than a person.
//
// **In a build of this tree it describes a request that is never made.**
// `taffy_asset_origin` defaults to empty (decision 0202), so
// `ProfileAssetPlane::Perform` refuses every fetch before this annotation is
// reached. The annotation stays, and stays accurate, because the argument
// stays: a build that points it at a release page makes exactly this request.
// Removing it would mean a future configuration change had to add an
// annotation nobody had reviewed, which is the thing an annotation is for.
constexpr net::NetworkTrafficAnnotationTag kAssetTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("taffy_asset_delivery", R"(
      semantics {
        sender: "TaffyGo asset delivery"
        description:
          "Fetches one of TaffyGo's own artifacts - a Python standard "
          "library, an on-device model, a filter list - from the delivery "
          "origin compiled into the product. The path, byte length and "
          "SHA-256 of every artifact are constants of the build; nothing a "
          "server says can change which bytes are accepted. No build of "
          "this product configures an origin: the artifacts a person needs "
          "are carried inside the application and installed from there, so "
          "this request is not made unless a build sets one."
        trigger:
          "An origin is configured in the build, the profile starts, and an "
          "artifact the product needs is absent - or the person asks for "
          "one. With no origin configured, nothing triggers it."
        data:
          "None. The request carries no cookies, no credentials, no browsing "
          "content and no identifier; it is a path the product compiled in."
        destination: OTHER
        internal { contacts { owners: "//taffy/OWNERS" } }
        user_data { type: NONE }
        last_reviewed: "2026-09-20"
      }
      policy {
        cookies_allowed: NO
        setting:
          "A person can decline an artifact, and can decline every artifact "
          "on a metered connection, from Taffy's downloads."
        policy_exception_justification: "No enterprise policy exists yet."
      })");

// Whether an HTTP status means asking again could reach a different answer.
bool IsTemporary(int status) {
  if (status == 0) {
    return true;  // Nothing was received; a transport failure.
  }
  if (status == net::HTTP_TOO_MANY_REQUESTS ||
      status == net::HTTP_REQUEST_TIMEOUT) {
    return true;
  }
  return status >= 500 && status < 600;
}

}  // namespace

AssetTransfer::AssetTransfer(
    scoped_refptr<network::SharedURLLoaderFactory> factory,
    scoped_refptr<base::SequencedTaskRunner> file_runner,
    base::FilePath store_root)
    : factory_(std::move(factory)),
      file_runner_(std::move(file_runner)),
      store_root_(std::move(store_root)) {}

AssetTransfer::~AssetTransfer() = default;

void AssetTransfer::Start(Request request,
                          ProgressCallback on_progress,
                          CompleteCallback on_complete) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  request_ = std::move(request);
  on_progress_ = std::move(on_progress);
  on_complete_ = std::move(on_complete);
  written_bytes_ = request_.offset_bytes;

  if (!request_.url.is_valid() || request_.total_bytes == 0 ||
      request_.total_bytes >
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      request_.offset_bytes >= request_.total_bytes) {
    // A URL the configuration refused, or a request that asks for no bytes.
    // Permanent: nothing about waiting changes a catalog row.
    Settle(AssetTransferOutcome::kOriginRefusedPermanent);
    return;
  }

  sink_ = base::SequenceBound<AssetTransferSink>(file_runner_, store_root_);
  sink_.AsyncCall(&AssetTransferSink::Open)
      .WithArgs(request_.asset_id, request_.asset_revision,
                request_.offset_bytes)
      .Then(base::BindOnce(&AssetTransfer::OnSinkOpened,
                           weak_factory_.GetWeakPtr()));
}

void AssetTransfer::OnSinkOpened(bool opened) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  if (!opened) {
    // The disk refused. Temporary, because a full disk empties and a locked
    // file unlocks, and because the alternative is a permanent refusal caused
    // by something that has nothing to do with the artifact.
    Settle(AssetTransferOutcome::kOriginRefusedTemporary);
    return;
  }

  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = request_.url;
  resource_request->method = net::HttpRequestHeaders::kGetMethod;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource_request->load_flags =
      net::LOAD_DO_NOT_SAVE_COOKIES | net::LOAD_BYPASS_CACHE;
  if (request_.offset_bytes > 0) {
    resource_request->headers.SetHeader(
        net::HttpRequestHeaders::kRange,
        base::StrCat({"bytes=",
                      base::NumberToString(request_.offset_bytes), "-"}));
  }

  loader_ = network::SimpleURLLoader::Create(std::move(resource_request),
                                             kAssetTrafficAnnotation);
  loader_->SetAllowHttpErrorResults(true);
  // The plane owns retry: it counts attempts, schedules the backoff and stops.
  // A second retry policy inside the loader would spend attempts the plane did
  // not count and could not stop.
  loader_->SetRetryOptions(0, network::SimpleURLLoader::RETRY_NEVER);
  loader_->SetOnResponseStartedCallback(base::BindOnce(
      &AssetTransfer::OnResponseStarted, weak_factory_.GetWeakPtr()));
  loader_->DownloadAsStream(factory_.get(), this);
}

void AssetTransfer::OnResponseStarted(
    const GURL& /*final_url*/,
    const network::mojom::URLResponseHead& head) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  if (!head.headers) {
    Settle(AssetTransferOutcome::kOriginRefusedTemporary);
    return;
  }
  const int status = head.headers->response_code();

  if (request_.offset_bytes > 0) {
    // A range was asked for. `206` is the only answer that may be appended to
    // what is already on disk. A `200` means the origin ignored the range and
    // is sending the whole artifact, which is a correct response to a request
    // this transfer cannot use — so it is refused here and the next attempt
    // starts from zero, rather than appending a whole file to a partial one.
    if (status != net::HTTP_PARTIAL_CONTENT) {
      Settle(status == net::HTTP_OK
                 ? AssetTransferOutcome::kIntegrityWrongLength
                 : (IsTemporary(status)
                        ? AssetTransferOutcome::kOriginRefusedTemporary
                        : AssetTransferOutcome::kOriginRefusedPermanent));
    }
    return;
  }

  if (status != net::HTTP_OK) {
    Settle(IsTemporary(status)
               ? AssetTransferOutcome::kOriginRefusedTemporary
               : AssetTransferOutcome::kOriginRefusedPermanent);
  }
}

void AssetTransfer::OnDataReceived(std::string_view chunk,
                                   base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  if (written_bytes_ > request_.total_bytes ||
      chunk.size() > request_.total_bytes - written_bytes_) {
    // More bytes than the catalog names. The artifact is not the one the
    // product was built against, and continuing would write past a length the
    // device budgeted for.
    Settle(AssetTransferOutcome::kIntegrityWrongLength);
    return;
  }
  sink_.AsyncCall(&AssetTransferSink::Write)
      .WithArgs(std::string(chunk))
      .Then(base::BindOnce(
          [](base::WeakPtr<AssetTransfer> self, base::OnceClosure resume,
             std::optional<uint64_t> written) {
            if (!self) {
              return;
            }
            if (!written.has_value()) {
              self->Settle(AssetTransferOutcome::kOriginRefusedTemporary);
              return;
            }
            self->OnChunkWritten(std::move(resume), *written);
          },
          weak_factory_.GetWeakPtr(), std::move(resume)));
}

void AssetTransfer::OnChunkWritten(base::OnceClosure resume,
                                   uint64_t written_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  written_bytes_ = written_bytes;
  if (on_progress_) {
    on_progress_.Run(written_bytes_);
  }
  // Asking for the next chunk only now is the whole of the back-pressure: a
  // slow disk slows the socket instead of filling memory with what it cannot
  // write.
  std::move(resume).Run();
}

void AssetTransfer::OnRetry(base::OnceClosure start_retry) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Unreachable while `RETRY_NEVER` is set, and a `NOTREACHED` here would be a
  // crash if that ever changed. Settling as interrupted is what the plane
  // already knows how to handle.
  Settle(AssetTransferOutcome::kInterrupted);
}

void AssetTransfer::OnComplete(bool success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  if (!success) {
    Settle(AssetTransferOutcome::kInterrupted);
    return;
  }
  if (written_bytes_ != request_.total_bytes) {
    // The transfer said it finished and the file is the wrong size. Not an
    // interruption — the origin ended the response deliberately — so this is
    // judged rather than resumed.
    Settle(AssetTransferOutcome::kIntegrityWrongLength);
    return;
  }
  sink_.AsyncCall(&AssetTransferSink::Finish)
      .Then(base::BindOnce(&AssetTransfer::OnDigest,
                           weak_factory_.GetWeakPtr()));
}

void AssetTransfer::OnDigest(std::array<uint8_t, 32> digest) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  observed_digest_ = digest;
  loader_.reset();
  if (!std::ranges::equal(digest, request_.expected_digest)) {
    Settle(AssetTransferOutcome::kIntegrityWrongDigest);
    return;
  }
  sink_.AsyncCall(&AssetTransferSink::Commit)
      .Then(base::BindOnce(&AssetTransfer::OnCommitted,
                           weak_factory_.GetWeakPtr()));
}

void AssetTransfer::OnCommitted(bool committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A commit that failed leaves sound bytes in staging, which the next scan
  // reports as partial and the next attempt resumes from — at zero cost,
  // because every byte is already there and the range request asks for none.
  Settle(committed ? AssetTransferOutcome::kInstalled
                   : AssetTransferOutcome::kOriginRefusedTemporary);
}

void AssetTransfer::Settle(AssetTransferOutcome outcome) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (settled_) {
    return;
  }
  settled_ = true;
  loader_.reset();
  AssetTransferReport report;
  report.outcome = outcome;
  report.written_bytes = written_bytes_;
  report.observed_digest = observed_digest_;
  if (on_complete_) {
    std::move(on_complete_).Run(report);
  }
}

}  // namespace taffy
