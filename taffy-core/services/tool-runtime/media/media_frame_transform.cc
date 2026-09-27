// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "build/buildflag.h"
#include "crypto/hash.h"
#include "media/base/demuxer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/base/media_tracks.h"
#include "media/base/media_util.h"
#include "media/base/pipeline_status.h"
#include "media/base/timestamp_constants.h"
#include "media/base/video_decoder.h"
#include "media/base/video_decoder_config.h"
#include "media/base/video_frame.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/ffmpeg_video_decoder.h"
#include "media/filters/file_data_source.h"
#include "media/media_buildflags.h"
#include "media/renderers/paint_canvas_video_renderer.h"
#include "taffy/services/tool-runtime/media/media_transform_internal.h"
#include "taffy/services/tool-runtime/media/media_zip_writer.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"

#if BUILDFLAG(ENABLE_DAV1D_DECODER)
#include "media/filters/dav1d_video_decoder.h"
#endif

#if BUILDFLAG(ENABLE_LIBVPX)
#include "media/filters/vpx_video_decoder.h"
#endif

namespace taffy::media_tool {
namespace {

struct FrameRecord {
  uint64_t timestamp_ms = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
};

std::unique_ptr<media::VideoDecoder> CreateDecoder(
    const media::VideoDecoderConfig& config) {
#if BUILDFLAG(ENABLE_LIBVPX)
  if (config.codec() == media::VideoCodec::kVP8 ||
      config.codec() == media::VideoCodec::kVP9) {
    return std::make_unique<media::VpxVideoDecoder>();
  }
#endif
#if BUILDFLAG(ENABLE_DAV1D_DECODER)
  if (config.codec() == media::VideoCodec::kAV1) {
    return std::make_unique<media::Dav1dVideoDecoder>(
        std::make_unique<media::NullMediaLog>());
  }
#endif
  if (media::FFmpegVideoDecoder::IsCodecSupported(config.codec())) {
    return std::make_unique<media::FFmpegVideoDecoder>(
        std::make_unique<media::NullMediaLog>());
  }
  return nullptr;
}

std::string BuildManifest(const std::vector<FrameRecord>& frames) {
  std::string out =
      "{\"format\":\"taffy-frame-samples-v1\",\"source\":"
      "\"broker-owned-media\",\"frames\":[";
  for (size_t index = 0; index < frames.size(); ++index) {
    if (index != 0u) {
      out.push_back(',');
    }
    const FrameRecord& frame = frames[index];
    out.append("{\"file\":\"");
    out.append(base::StringPrintf("frame-%04u.png",
                                  static_cast<unsigned>(index + 1u)));
    out.append("\",\"timestamp_ms\":");
    out.append(base::NumberToString(frame.timestamp_ms));
    out.append(",\"width\":");
    out.append(base::NumberToString(frame.width));
    out.append(",\"height\":");
    out.append(base::NumberToString(frame.height));
    out.push_back('}');
  }
  out.append("]}");
  return out;
}

class FrameTransformRun final : public media::DemuxerHost {
 public:
  FrameTransformRun(TransformLimits limits,
                    std::shared_ptr<std::atomic_bool> cancelled,
                    TransformCallback callback)
      : limits_(limits),
        cancelled_(std::move(cancelled)),
        callback_(std::move(callback)) {}
  FrameTransformRun(const FrameTransformRun&) = delete;
  FrameTransformRun& operator=(const FrameTransformRun&) = delete;
  ~FrameTransformRun() override = default;

  static void Start(std::unique_ptr<FrameTransformRun> run,
                    base::File input,
                    base::File output) {
    FrameTransformRun* const raw = run.get();
    raw->self_ = std::move(run);
    raw->Begin(std::move(input), std::move(output));
  }

  void OnBufferedTimeRangesChanged(
      const media::Ranges<base::TimeDelta>& ranges) override {}
  void SetDuration(base::TimeDelta duration) override { duration_ = duration; }
  void OnDemuxerError(media::PipelineStatus error) override {
    Finish(base::unexpected(TransformFailure::kMalformedMedia));
  }

 private:
  bool Interrupted() {
    if (cancelled_ && cancelled_->load(std::memory_order_relaxed)) {
      pending_failure_ = TransformFailure::kCancelled;
      return true;
    }
    if (!limits_.deadline.is_null() &&
        base::TimeTicks::Now() >= limits_.deadline) {
      pending_failure_ = TransformFailure::kDeadlineExceeded;
      return true;
    }
    return false;
  }

  void Begin(base::File input, base::File output) {
    if (!input.IsValid()) {
      Finish(base::unexpected(TransformFailure::kUnreadableInput));
      return;
    }
    const int64_t length = input.GetLength();
    if (length < 0) {
      Finish(base::unexpected(TransformFailure::kUnreadableInput));
      return;
    }
    if (static_cast<uint64_t>(length) > limits_.max_input_bytes) {
      Finish(base::unexpected(TransformFailure::kInputTooLarge));
      return;
    }
    if (!output.IsValid() || !output.SetLength(0) ||
        output.Seek(base::File::FROM_BEGIN, 0) != 0) {
      Finish(base::unexpected(TransformFailure::kUnwritableOutput));
      return;
    }
    output_ = std::move(output);
    zip_ = std::make_unique<MediaZipWriter>(
        static_cast<size_t>(limits_.max_output_bytes));
    if (!source_.Initialize(std::move(input))) {
      Finish(base::unexpected(TransformFailure::kUnreadableInput));
      return;
    }
    demuxer_ = std::make_unique<media::FFmpegDemuxer>(
        base::SequencedTaskRunner::GetCurrentDefault(), &source_,
        base::DoNothing(), base::DoNothing(), &media_log_,
        /*is_local_file=*/true);
    demuxer_->Initialize(this, base::BindOnce(&FrameTransformRun::OnDemuxed,
                                              weak_factory_.GetWeakPtr()));
  }

  void OnDemuxed(media::PipelineStatus status) {
    if (!status.is_ok() || Interrupted()) {
      Finish(base::unexpected(
          pending_failure_.value_or(TransformFailure::kMalformedMedia)));
      return;
    }
    if (duration_.is_positive() && duration_ != media::kInfiniteDuration &&
        duration_ > limits_.max_duration) {
      Finish(base::unexpected(TransformFailure::kDurationTooLong));
      return;
    }
    for (media::DemuxerStream* candidate : demuxer_->GetAllStreams()) {
      if (candidate && candidate->type() == media::DemuxerStream::VIDEO) {
        stream_ = candidate;
        break;
      }
    }
    if (!stream_) {
      Finish(base::unexpected(TransformFailure::kNoMatchingStream));
      return;
    }
    const media::VideoDecoderConfig config = stream_->video_decoder_config();
    const gfx::Size size = config.natural_size();
    if (!config.IsValidConfig() || config.is_encrypted() || size.IsEmpty()) {
      Finish(base::unexpected(TransformFailure::kUnsupportedCodec));
      return;
    }
    if (size.width() > static_cast<int>(limits_.max_width) ||
        size.height() > static_cast<int>(limits_.max_height)) {
      Finish(base::unexpected(TransformFailure::kDimensionsTooLarge));
      return;
    }
    decoder_ = CreateDecoder(config);
    if (!decoder_) {
      Finish(base::unexpected(TransformFailure::kUnsupportedCodec));
      return;
    }
    decoder_->Initialize(config, /*low_delay=*/false, nullptr,
                         base::BindOnce(&FrameTransformRun::OnDecoderReady,
                                        weak_factory_.GetWeakPtr()),
                         base::BindRepeating(&FrameTransformRun::OnFrame,
                                             weak_factory_.GetWeakPtr()),
                         base::DoNothing());
  }

  void OnDecoderReady(media::DecoderStatus status) {
    if (!status.is_ok() || Interrupted()) {
      Finish(base::unexpected(
          pending_failure_.value_or(TransformFailure::kUnsupportedCodec)));
      return;
    }
    Read();
  }

  void Read() {
    if (Interrupted()) {
      Finish(base::unexpected(*pending_failure_));
      return;
    }
    stream_->Read(1u, base::BindOnce(&FrameTransformRun::OnRead,
                                     weak_factory_.GetWeakPtr()));
  }

  void OnRead(media::DemuxerStream::Status status,
              media::DemuxerStream::DecoderBufferVector buffers) {
    if (status != media::DemuxerStream::kOk || buffers.size() != 1u ||
        Interrupted()) {
      Finish(base::unexpected(
          pending_failure_.value_or(TransformFailure::kMalformedMedia)));
      return;
    }
    if (++decoded_buffers_ > limits_.max_decoded_buffers) {
      Finish(base::unexpected(TransformFailure::kTooManyFrames));
      return;
    }
    decoding_eos_ = buffers.front()->end_of_stream();
    decoder_->Decode(std::move(buffers.front()),
                     base::BindOnce(&FrameTransformRun::OnDecoded,
                                    weak_factory_.GetWeakPtr()));
  }

  bool ShouldSample(base::TimeDelta timestamp) const {
    if (frames_.empty()) {
      return true;
    }
    if (!duration_.is_positive() || duration_ == media::kInfiniteDuration ||
        timestamp == media::kNoTimestamp) {
      return true;
    }
    const int64_t target_ms = duration_.InMilliseconds() *
                              static_cast<int64_t>(frames_.size()) /
                              static_cast<int64_t>(limits_.max_frames);
    return timestamp.InMilliseconds() >= target_ms;
  }

  void OnFrame(scoped_refptr<media::VideoFrame> frame) {
    if (pending_failure_ || !frame || frames_.size() >= limits_.max_frames ||
        !ShouldSample(frame->timestamp())) {
      return;
    }
    if (!frame->HasDirectCpuAccess()) {
      pending_failure_ = TransformFailure::kUnsupportedCodec;
      return;
    }
    const gfx::Size size = frame->visible_rect().size();
    if (size.IsEmpty() || size.width() > static_cast<int>(limits_.max_width) ||
        size.height() > static_cast<int>(limits_.max_height)) {
      pending_failure_ = TransformFailure::kDimensionsTooLarge;
      return;
    }
    const uint64_t pixel_bytes = static_cast<uint64_t>(size.width()) *
                                 static_cast<uint64_t>(size.height()) * 4u;
    if (pixel_bytes > limits_.max_output_bytes) {
      pending_failure_ = TransformFailure::kOutputTooLarge;
      return;
    }
    SkBitmap bitmap;
    if (!bitmap.tryAllocN32Pixels(size.width(), size.height())) {
      pending_failure_ = TransformFailure::kOutputTooLarge;
      return;
    }
    media::PaintCanvasVideoRenderer::ConvertVideoFrameToRGBPixels(
        frame.get(),
        // `tryAllocN32Pixels` allocated exactly computeByteSize() writable
        // bytes. Chromium's span constructor cannot infer that from a void*.
        UNSAFE_BUFFERS(base::span(static_cast<uint8_t*>(bitmap.getPixels()),
                                  bitmap.computeByteSize())),
        bitmap.rowBytes(), kN32_SkColorType, /*premultiply_alpha=*/true,
        media::PaintCanvasVideoRenderer::kFilterBilinear,
        /*disable_threading=*/true);
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/true);
    if (!png || png->empty()) {
      pending_failure_ = TransformFailure::kMalformedMedia;
      return;
    }
    const std::string name = base::StringPrintf(
        "frame-%04u.png", static_cast<unsigned>(frames_.size() + 1u));
    if (!zip_->Add(name, *png)) {
      pending_failure_ = TransformFailure::kOutputTooLarge;
      return;
    }
    const base::TimeDelta timestamp = frame->timestamp();
    frames_.push_back(
        FrameRecord{timestamp.is_positive()
                        ? static_cast<uint64_t>(timestamp.InMilliseconds())
                        : 0u,
                    static_cast<uint32_t>(size.width()),
                    static_cast<uint32_t>(size.height())});
  }

  void OnDecoded(media::DecoderStatus status) {
    if (!status.is_ok()) {
      Finish(base::unexpected(TransformFailure::kMalformedMedia));
      return;
    }
    if (pending_failure_) {
      Finish(base::unexpected(*pending_failure_));
      return;
    }
    if (!decoding_eos_ && frames_.size() < limits_.max_frames) {
      Read();
      return;
    }
    CompleteArchive();
  }

  void CompleteArchive() {
    if (frames_.empty()) {
      Finish(base::unexpected(TransformFailure::kNoMatchingStream));
      return;
    }
    const std::string manifest = BuildManifest(frames_);
    if (!zip_->Add("manifest.json", base::as_byte_span(manifest))) {
      Finish(base::unexpected(TransformFailure::kOutputTooLarge));
      return;
    }
    std::vector<uint8_t> archive;
    if (!zip_->Finish(&archive) || archive.empty() ||
        archive.size() > limits_.max_output_bytes) {
      Finish(base::unexpected(TransformFailure::kOutputTooLarge));
      return;
    }
    const std::array<uint8_t, crypto::hash::kSha256Size> digest =
        crypto::hash::Sha256(archive);
    if (!output_.WriteAtCurrentPosAndCheck(archive) || !output_.Flush()) {
      Finish(base::unexpected(TransformFailure::kUnwritableOutput));
      return;
    }
    Finish(TransformReading{archive.size(),
                            static_cast<uint32_t>(frames_.size()), digest});
  }

  void Finish(TransformResult result) {
    if (!callback_) {
      return;
    }
    weak_factory_.InvalidateWeakPtrs();
    decoder_.reset();
    if (demuxer_) {
      demuxer_->Stop();
    } else {
      source_.Stop();
    }
    if (!result.has_value() && output_.IsValid()) {
      output_.SetLength(0);
    }
    std::move(callback_).Run(std::move(result));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(self_));
  }

  const TransformLimits limits_;
  const std::shared_ptr<std::atomic_bool> cancelled_;
  TransformCallback callback_;
  std::unique_ptr<FrameTransformRun> self_;
  media::NullMediaLog media_log_;
  media::FileDataSource source_;
  std::unique_ptr<media::FFmpegDemuxer> demuxer_;
  std::unique_ptr<media::VideoDecoder> decoder_;
  raw_ptr<media::DemuxerStream> stream_ = nullptr;
  std::unique_ptr<MediaZipWriter> zip_;
  base::File output_;
  base::TimeDelta duration_;
  std::optional<TransformFailure> pending_failure_;
  std::vector<FrameRecord> frames_;
  uint32_t decoded_buffers_ = 0u;
  bool decoding_eos_ = false;
  base::WeakPtrFactory<FrameTransformRun> weak_factory_{this};
};

}  // namespace

void RunFrameTransform(base::File input,
                       base::File output,
                       TransformLimits limits,
                       std::shared_ptr<std::atomic_bool> cancelled,
                       TransformCallback callback) {
  FrameTransformRun::Start(
      std::make_unique<FrameTransformRun>(limits, std::move(cancelled),
                                          std::move(callback)),
      std::move(input), std::move(output));
}

}  // namespace taffy::media_tool
