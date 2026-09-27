// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/assets/asset_transfer.h"

#include <stdint.h>

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/byte_size.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "crypto/hash.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "net/http/http_version.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/url_loader_completion_status.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/test/test_url_loader_factory.h"
#include "taffy/browser/assets/asset_store.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {
namespace {

constexpr char kUrl[] = "https://assets.example/python/3.14.2/stdlib.zip";
constexpr char kAssetId[] = "python-stdlib";
constexpr char kRevision[] = "3.14.2";

// The artifact under test: short enough that a failing test reads as
// arithmetic, long enough that a resume splits it somewhere interesting.
constexpr std::string_view kArtifact =
    "the bytes of a standard library, or near enough for a test";

std::array<uint8_t, 32> DigestOf(std::string_view bytes) {
  std::array<uint8_t, 32> digest = {};
  crypto::hash::Hash(crypto::hash::kSha256, bytes, digest);
  return digest;
}

class AssetTransferTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    file_runner_ =
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
    shared_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &factory_);
  }

  AssetStore Store() const { return AssetStore(directory_.GetPath()); }

  // Puts `bytes` in staging, as an interrupted transfer would have left them.
  void Stage(std::string_view bytes) {
    base::File file = Store().OpenStaging(kAssetId, kRevision, 0);
    ASSERT_TRUE(file.IsValid());
    ASSERT_TRUE(file.WriteAtCurrentPosAndCheck(base::as_byte_span(bytes)));
  }

  // Arms the origin's one answer.
  void Respond(net::HttpStatusCode status, std::string_view body) {
    const std::string status_line =
        base::StrCat({base::NumberToString(static_cast<int>(status)), " ",
                      net::GetHttpReasonPhrase(status)});
    auto head = network::mojom::URLResponseHead::New();
    head->headers =
        net::HttpResponseHeaders::Builder(net::HttpVersion(1, 1), status_line)
            .Build();
    network::URLLoaderCompletionStatus completion(net::OK);
    completion.decoded_body_length = base::ByteSize(body.size());
    factory_.AddResponse(GURL(kUrl), std::move(head), std::string(body),
                         completion);
  }

  // Runs one transfer to completion and answers what it reported.
  AssetTransferReport Run(uint64_t offset,
                          uint64_t total_bytes,
                          std::array<uint8_t, 32> expected_digest,
                          std::string_view url = kUrl) {
    AssetTransfer transfer(shared_factory_, file_runner_,
                           directory_.GetPath());
    AssetTransfer::Request request;
    request.url = GURL(url);
    request.asset_id = kAssetId;
    request.asset_revision = kRevision;
    request.offset_bytes = offset;
    request.total_bytes = total_bytes;
    request.expected_digest = expected_digest;

    AssetTransferReport reported;
    base::RunLoop loop;
    transfer.Start(std::move(request), base::DoNothing(),
                   base::BindLambdaForTesting([&](AssetTransferReport report) {
                     reported = report;
                     loop.Quit();
                   }));
    loop.Run();
    // The sink runs on its own sequence; let it drain before the disk is read.
    task_environment_.RunUntilIdle();
    return reported;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  base::ScopedTempDir directory_;
  scoped_refptr<base::SequencedTaskRunner> file_runner_;
  network::TestURLLoaderFactory factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_factory_;
};

TEST_F(AssetTransferTest, SoundBytesAreVerifiedAndInstalledInOneOperation) {
  Respond(net::HTTP_OK, kArtifact);
  const AssetTransferReport report =
      Run(0, kArtifact.size(), DigestOf(kArtifact));

  EXPECT_EQ(report.outcome, AssetTransferOutcome::kInstalled);
  EXPECT_EQ(report.written_bytes, kArtifact.size());
  EXPECT_EQ(report.observed_digest, DigestOf(kArtifact));

  // Installed, and nothing left staged. A caller told "installed" that then
  // found a partial file would have been told two different things.
  const std::vector<AssetStore::Found> found = Store().Scan();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_TRUE(found[0].installed);
  EXPECT_EQ(found[0].staged_bytes, 0u);
  EXPECT_EQ(found[0].installed_bytes, kArtifact.size());
}

TEST_F(AssetTransferTest, BytesThatHashToSomethingElseAreNeverInstalled) {
  Respond(net::HTTP_OK, kArtifact);
  std::array<uint8_t, 32> wrong = DigestOf(kArtifact);
  wrong[0] ^= 0xff;

  const AssetTransferReport report = Run(0, kArtifact.size(), wrong);
  EXPECT_EQ(report.outcome, AssetTransferOutcome::kIntegrityWrongDigest);
  // The observed digest is reported, so the plane can say what it got.
  EXPECT_EQ(report.observed_digest, DigestOf(kArtifact));

  const std::vector<AssetStore::Found> found = Store().Scan();
  ASSERT_EQ(found.size(), 1u);
  EXPECT_FALSE(found[0].installed);
}

TEST_F(AssetTransferTest, FewerBytesThanTheCatalogNamesIsALengthVerdict) {
  Respond(net::HTTP_OK, kArtifact.substr(0, 10));
  EXPECT_EQ(Run(0, kArtifact.size(), DigestOf(kArtifact)).outcome,
            AssetTransferOutcome::kIntegrityWrongLength);
}

TEST_F(AssetTransferTest, MoreBytesThanTheCatalogNamesStopsMidStream) {
  Respond(net::HTTP_OK, std::string(kArtifact) + " and then some");
  const AssetTransferReport report =
      Run(0, kArtifact.size(), DigestOf(kArtifact));
  EXPECT_EQ(report.outcome, AssetTransferOutcome::kIntegrityWrongLength);
  EXPECT_LE(report.written_bytes, kArtifact.size())
      << "the transfer must not write past the length the device budgeted for";
}

TEST_F(AssetTransferTest, AResumeHashesThePrefixAndInstallsTheWholeArtifact) {
  // Half the artifact is already staged. The rest arrives as a 206, and the
  // digest that decides is over both halves — which is the whole point of
  // re-hashing the prefix rather than storing a hash state.
  const size_t split = kArtifact.size() / 2;
  ASSERT_NO_FATAL_FAILURE(Stage(kArtifact.substr(0, split)));

  Respond(net::HTTP_PARTIAL_CONTENT, kArtifact.substr(split));
  const AssetTransferReport report =
      Run(split, kArtifact.size(), DigestOf(kArtifact));

  EXPECT_EQ(report.outcome, AssetTransferOutcome::kInstalled);
  EXPECT_EQ(report.written_bytes, kArtifact.size());
  EXPECT_EQ(report.observed_digest, DigestOf(kArtifact));
}

TEST_F(AssetTransferTest, AnOriginThatIgnoresTheRangeIsRefusedNotAppended) {
  // The origin answered a range request with the whole artifact. Appending it
  // to what is already staged would produce a file of the wrong length that
  // then failed its digest for a reason nobody could read. Refusing here is
  // what makes the next attempt start from zero.
  const size_t split = kArtifact.size() / 2;
  ASSERT_NO_FATAL_FAILURE(Stage(kArtifact.substr(0, split)));

  Respond(net::HTTP_OK, kArtifact);
  EXPECT_EQ(Run(split, kArtifact.size(), DigestOf(kArtifact)).outcome,
            AssetTransferOutcome::kIntegrityWrongLength);
}

TEST_F(AssetTransferTest, AServerErrorIsTemporaryAndAMissingFileIsNot) {
  // Both answers carry no body at all, which is the case a transfer that only
  // judged its first chunk would never see.
  Respond(net::HTTP_INTERNAL_SERVER_ERROR, "");
  EXPECT_EQ(Run(0, kArtifact.size(), DigestOf(kArtifact)).outcome,
            AssetTransferOutcome::kOriginRefusedTemporary);

  Respond(net::HTTP_NOT_FOUND, "");
  EXPECT_EQ(Run(0, kArtifact.size(), DigestOf(kArtifact)).outcome,
            AssetTransferOutcome::kOriginRefusedPermanent);
}

TEST_F(AssetTransferTest, AUrlTheConfigurationRefusedNeverTouchesTheDisk) {
  const AssetTransferReport report =
      Run(0, kArtifact.size(), DigestOf(kArtifact), "");
  EXPECT_EQ(report.outcome, AssetTransferOutcome::kOriginRefusedPermanent);
  EXPECT_TRUE(Store().Scan().empty());
}

TEST_F(AssetTransferTest, AnOffsetAtOrPastTheEndIsRefusedRatherThanRequested) {
  EXPECT_EQ(Run(kArtifact.size(), kArtifact.size(), DigestOf(kArtifact)).outcome,
            AssetTransferOutcome::kOriginRefusedPermanent);
  EXPECT_FALSE(factory_.IsPending(kUrl))
      << "nothing may be asked of the origin for a range that cannot exist";
}

TEST_F(AssetTransferTest, ALengthTheFileApiCannotRepresentIsRefusedUpFront) {
  const uint64_t too_large =
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1u;
  const AssetTransferReport report =
      Run(0, too_large, DigestOf(kArtifact));

  EXPECT_EQ(report.outcome, AssetTransferOutcome::kOriginRefusedPermanent);
  EXPECT_FALSE(factory_.IsPending(kUrl));
  EXPECT_TRUE(Store().Scan().empty());
}

}  // namespace
}  // namespace taffy
