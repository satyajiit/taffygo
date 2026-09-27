// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_probe.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "media/base/demuxer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_tracks.h"
#include "media/base/media_util.h"
#include "media/base/pipeline_status.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_decoder_config.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/file_data_source.h"
#include "ui/gfx/geometry/size.h"

namespace taffy::media_tool {

namespace {

// One probe, alive for exactly one file.
//
// The demuxer is asynchronous and the reading is assembled from three places —
// the host callback for duration, the stream list for kinds and dimensions,
// and the initialization status for whether any of it may be believed — so the
// run owns itself until it has all three or has failed. `Finish` is the only
// exit, and it runs the reply exactly once.
class ProbeRun final : public media::DemuxerHost {
 public:
  ProbeRun(uint64_t max_input_bytes, ProbeCallback callback)
      : max_input_bytes_(max_input_bytes), callback_(std::move(callback)) {}

  ProbeRun(const ProbeRun&) = delete;
  ProbeRun& operator=(const ProbeRun&) = delete;

  ~ProbeRun() override = default;

  // Takes ownership of itself for the duration of the parse.
  static void Start(std::unique_ptr<ProbeRun> run, base::File file) {
    ProbeRun* const raw = run.get();
    raw->self_ = std::move(run);
    raw->Begin(std::move(file));
  }

  // media::DemuxerHost
  void OnBufferedTimeRangesChanged(
      const media::Ranges<base::TimeDelta>& ranges) override {}

  void SetDuration(base::TimeDelta duration) override { duration_ = duration; }

  void OnDemuxerError(media::PipelineStatus error) override {
    Finish(base::unexpected(ProbeFailure::kUnparseableContainer));
  }

 private:
  void Begin(base::File file) {
    if (!file.IsValid()) {
      Finish(base::unexpected(ProbeFailure::kUnreadableFile));
      return;
    }
    const int64_t length = file.GetLength();
    if (length < 0) {
      Finish(base::unexpected(ProbeFailure::kUnreadableFile));
      return;
    }
    if (static_cast<uint64_t>(length) > max_input_bytes_) {
      Finish(base::unexpected(ProbeFailure::kInputTooLarge));
      return;
    }
    if (!source_.Initialize(std::move(file))) {
      Finish(base::unexpected(ProbeFailure::kUnreadableFile));
      return;
    }
    // `is_local_file` is true because the descriptor came from the browser's
    // own handle broker and is mapped, not streamed. It tells the demuxer it
    // may seek freely, which is what makes a single bounded pass enough.
    demuxer_ = std::make_unique<media::FFmpegDemuxer>(
        base::SequencedTaskRunner::GetCurrentDefault(), &source_,
        base::DoNothing(), base::DoNothing(), &media_log_,
        /*is_local_file=*/true);
    demuxer_->Initialize(this, base::BindOnce(&ProbeRun::OnInitialized,
                                              weak_factory_.GetWeakPtr()));
  }

  void OnInitialized(media::PipelineStatus status) {
    if (!status.is_ok()) {
      Finish(base::unexpected(ProbeFailure::kUnparseableContainer));
      return;
    }

    MediaReading reading;
    for (media::DemuxerStream* const stream : demuxer_->GetAllStreams()) {
      if (!stream) {
        continue;
      }
      switch (stream->type()) {
        case media::DemuxerStream::AUDIO:
          ++reading.audio_streams;
          break;
        case media::DemuxerStream::VIDEO: {
          ++reading.video_streams;
          const gfx::Size size = stream->video_decoder_config().natural_size();
          if (size.width() > 0 && size.height() > 0) {
            const uint32_t width = base::checked_cast<uint32_t>(size.width());
            const uint32_t height = base::checked_cast<uint32_t>(size.height());
            // The largest video stream is the one a person is shown; an
            // attached cover image is a video stream too, and is smaller.
            if (static_cast<uint64_t>(width) * height >
                static_cast<uint64_t>(reading.width) * reading.height) {
              reading.width = width;
              reading.height = height;
            }
          }
          break;
        }
        case media::DemuxerStream::UNKNOWN:
          break;
      }
    }

    if (reading.audio_streams == 0 && reading.video_streams == 0) {
      Finish(base::unexpected(ProbeFailure::kNoStreams));
      return;
    }

    // A container may declare no duration, an infinite one, or a negative one.
    // Each is reported as zero rather than as an enormous number, because the
    // field is unsigned and a wrapped duration is a value that reads as true.
    if (duration_.is_positive() && duration_ != media::kInfiniteDuration) {
      const int64_t milliseconds = duration_.InMilliseconds();
      if (milliseconds > 0) {
        reading.duration_ms = static_cast<uint64_t>(milliseconds);
      }
    }

    Finish(std::move(reading));
  }

  void Finish(ProbeResult result) {
    if (!callback_) {
      return;
    }
    // The demuxer must be stopped before it is destroyed, and its weak
    // pointers must be gone: both are its own documented contract.
    weak_factory_.InvalidateWeakPtrs();
    if (demuxer_) {
      // Stop() stops the data source too, and the order inside it matters:
      // aborting the reader first can free a buffer FFmpeg is still filling.
      demuxer_->Stop();
    } else {
      source_.Stop();
    }
    std::move(callback_).Run(std::move(result));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(self_));
  }

  const uint64_t max_input_bytes_;
  ProbeCallback callback_;
  std::unique_ptr<ProbeRun> self_;
  media::NullMediaLog media_log_;
  media::FileDataSource source_;
  std::unique_ptr<media::FFmpegDemuxer> demuxer_;
  base::TimeDelta duration_;
  base::WeakPtrFactory<ProbeRun> weak_factory_{this};
};

}  // namespace

void ProbeMedia(base::File file,
                uint64_t max_input_bytes,
                ProbeCallback callback) {
  ProbeRun::Start(
      std::make_unique<ProbeRun>(max_input_bytes, std::move(callback)),
      std::move(file));
}

}  // namespace taffy::media_tool
