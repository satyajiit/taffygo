// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "media/base/audio_buffer.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/audio_sample_types.h"
#include "media/base/demuxer.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/base/media_tracks.h"
#include "media/base/media_util.h"
#include "media/base/pipeline_status.h"
#include "media/base/timestamp_constants.h"
#include "media/filters/ffmpeg_audio_decoder.h"
#include "media/filters/ffmpeg_demuxer.h"
#include "media/filters/file_data_source.h"
#include "crypto/hash.h"
#include "taffy/services/tool-runtime/media/media_transform_internal.h"

namespace taffy::media_tool {
namespace {

constexpr size_t kWaveHeaderBytes = 44u;
constexpr uint32_t kMaxAudioChannels = 2u;
constexpr uint32_t kMaxSampleRate = 48000u;

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  AppendU16(out, static_cast<uint16_t>(value));
  AppendU16(out, static_cast<uint16_t>(value >> 16u));
}

void AppendTag(std::vector<uint8_t>* out, std::string_view value) {
  out->insert(out->end(), value.begin(), value.end());
}

std::vector<uint8_t> WaveHeader(uint16_t channels,
                                uint32_t sample_rate,
                                uint32_t data_bytes) {
  const uint16_t block_align = channels * 2u;
  std::vector<uint8_t> out;
  out.reserve(kWaveHeaderBytes);
  AppendTag(&out, "RIFF");
  AppendU32(&out, 36u + data_bytes);
  AppendTag(&out, "WAVE");
  AppendTag(&out, "fmt ");
  AppendU32(&out, 16u);
  AppendU16(&out, 1u);  // linear PCM
  AppendU16(&out, channels);
  AppendU32(&out, sample_rate);
  AppendU32(&out, sample_rate * block_align);
  AppendU16(&out, block_align);
  AppendU16(&out, 16u);
  AppendTag(&out, "data");
  AppendU32(&out, data_bytes);
  return out;
}

class AudioTransformRun final : public media::DemuxerHost {
 public:
  AudioTransformRun(bool mono,
                    TransformLimits limits,
                    std::shared_ptr<std::atomic_bool> cancelled,
                    TransformCallback callback)
      : mono_(mono),
        limits_(limits),
        cancelled_(std::move(cancelled)),
        callback_(std::move(callback)) {}

  AudioTransformRun(const AudioTransformRun&) = delete;
  AudioTransformRun& operator=(const AudioTransformRun&) = delete;
  ~AudioTransformRun() override = default;

  static void Start(std::unique_ptr<AudioTransformRun> run,
                    base::File input,
                    base::File output) {
    AudioTransformRun* const raw = run.get();
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
    const std::vector<uint8_t> placeholder(kWaveHeaderBytes, 0u);
    if (!output_.WriteAtCurrentPosAndCheck(placeholder)) {
      Finish(base::unexpected(TransformFailure::kUnwritableOutput));
      return;
    }
    if (!source_.Initialize(std::move(input))) {
      Finish(base::unexpected(TransformFailure::kUnreadableInput));
      return;
    }
    demuxer_ = std::make_unique<media::FFmpegDemuxer>(
        base::SequencedTaskRunner::GetCurrentDefault(), &source_,
        base::DoNothing(), base::DoNothing(), &media_log_,
        /*is_local_file=*/true);
    demuxer_->Initialize(this, base::BindOnce(&AudioTransformRun::OnDemuxed,
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
      if (candidate && candidate->type() == media::DemuxerStream::AUDIO) {
        stream_ = candidate;
        break;
      }
    }
    if (!stream_) {
      Finish(base::unexpected(TransformFailure::kNoMatchingStream));
      return;
    }
    const media::AudioDecoderConfig config = stream_->audio_decoder_config();
    if (!config.IsValidConfig() || config.is_encrypted() ||
        config.channels() <= 0 ||
        config.channels() > static_cast<int>(kMaxAudioChannels) ||
        config.samples_per_second() <= 0 ||
        config.samples_per_second() > static_cast<int>(kMaxSampleRate)) {
      Finish(base::unexpected(TransformFailure::kUnsupportedCodec));
      return;
    }
    input_channels_ = config.channels();
    output_channels_ = mono_ ? 1 : input_channels_;
    sample_rate_ = config.samples_per_second();
    decoder_ = std::make_unique<media::FFmpegAudioDecoder>(
        base::SequencedTaskRunner::GetCurrentDefault(), &media_log_);
    decoder_->Initialize(config, nullptr,
                         base::BindOnce(&AudioTransformRun::OnDecoderReady,
                                        weak_factory_.GetWeakPtr()),
                         base::BindRepeating(&AudioTransformRun::OnAudio,
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
    stream_->Read(1u, base::BindOnce(&AudioTransformRun::OnRead,
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
                     base::BindOnce(&AudioTransformRun::OnDecoded,
                                    weak_factory_.GetWeakPtr()));
  }

  void OnAudio(scoped_refptr<media::AudioBuffer> buffer) {
    if (pending_failure_ || !buffer || buffer->frame_count() <= 0 ||
        buffer->channel_count() != input_channels_ ||
        buffer->sample_rate() != sample_rate_) {
      pending_failure_ = TransformFailure::kMalformedMedia;
      return;
    }
    const uint64_t frames = static_cast<uint64_t>(buffer->frame_count());
    const uint64_t bytes =
        frames * static_cast<uint64_t>(output_channels_) * 2u;
    if (frames > std::numeric_limits<uint32_t>::max() ||
        data_bytes_ > std::numeric_limits<uint32_t>::max() - bytes ||
        kWaveHeaderBytes + data_bytes_ + bytes > limits_.max_output_bytes) {
      pending_failure_ = TransformFailure::kOutputTooLarge;
      return;
    }
    const auto bus = media::AudioBuffer::WrapOrCopyToAudioBus(buffer);
    if (!bus) {
      pending_failure_ = TransformFailure::kMalformedMedia;
      return;
    }
    std::vector<int16_t> samples(static_cast<size_t>(frames) *
                                 output_channels_);
    if (!mono_) {
      bus->ToInterleaved<media::SignedInt16SampleTypeTraits>(samples);
    } else {
      for (size_t frame = 0; frame < frames; ++frame) {
        float mixed = 0.0f;
        for (int channel = 0; channel < input_channels_; ++channel) {
          mixed += bus->channel(channel)[frame];
        }
        mixed /= static_cast<float>(input_channels_);
        samples[frame] = media::SignedInt16SampleTypeTraits::FromFloat(mixed);
      }
    }
    if (!output_.WriteAtCurrentPosAndCheck(base::as_byte_span(samples))) {
      pending_failure_ = TransformFailure::kUnwritableOutput;
      return;
    }
    data_bytes_ += bytes;
    decoded_frames_ += frames;
    const uint64_t max_frames =
        static_cast<uint64_t>(sample_rate_) * limits_.max_duration.InSeconds();
    if (decoded_frames_ > max_frames) {
      pending_failure_ = TransformFailure::kDurationTooLong;
    }
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
    if (!decoding_eos_) {
      Read();
      return;
    }
    if (data_bytes_ == 0u) {
      Finish(base::unexpected(TransformFailure::kNoMatchingStream));
      return;
    }
    const auto header = WaveHeader(static_cast<uint16_t>(output_channels_),
                                   static_cast<uint32_t>(sample_rate_),
                                   static_cast<uint32_t>(data_bytes_));
    if (!output_.WriteAndCheck(0, header) || !output_.Flush() ||
        output_.Seek(base::File::FROM_BEGIN, 0) != 0) {
      Finish(base::unexpected(TransformFailure::kUnwritableOutput));
      return;
    }
    std::array<uint8_t, crypto::hash::kSha256Size> digest{};
    if (!crypto::hash::HashFile(crypto::hash::kSha256, &output_, digest)) {
      Finish(base::unexpected(TransformFailure::kUnwritableOutput));
      return;
    }
    Finish(TransformReading{kWaveHeaderBytes + data_bytes_, 0u, digest});
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

  const bool mono_;
  const TransformLimits limits_;
  const std::shared_ptr<std::atomic_bool> cancelled_;
  TransformCallback callback_;
  std::unique_ptr<AudioTransformRun> self_;
  media::NullMediaLog media_log_;
  media::FileDataSource source_;
  std::unique_ptr<media::FFmpegDemuxer> demuxer_;
  std::unique_ptr<media::FFmpegAudioDecoder> decoder_;
  raw_ptr<media::DemuxerStream> stream_ = nullptr;
  base::File output_;
  base::TimeDelta duration_;
  std::optional<TransformFailure> pending_failure_;
  int input_channels_ = 0;
  int output_channels_ = 0;
  int sample_rate_ = 0;
  uint64_t data_bytes_ = 0u;
  uint64_t decoded_frames_ = 0u;
  uint32_t decoded_buffers_ = 0u;
  bool decoding_eos_ = false;
  base::WeakPtrFactory<AudioTransformRun> weak_factory_{this};
};

}  // namespace

void RunAudioTransform(bool mono,
                       base::File input,
                       base::File output,
                       TransformLimits limits,
                       std::shared_ptr<std::atomic_bool> cancelled,
                       TransformCallback callback) {
  AudioTransformRun::Start(
      std::make_unique<AudioTransformRun>(mono, limits, std::move(cancelled),
                                          std::move(callback)),
      std::move(input), std::move(output));
}

}  // namespace taffy::media_tool
