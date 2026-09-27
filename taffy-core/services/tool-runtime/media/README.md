# Media tool worker

[Current] The sandboxed media runtime: one process, one job, one terminal
result. It is the first tool worker with an implementation, and it exists as
much to make the seam real as to read a media file — until something ran out
of process, every claim about isolation, bounds and concurrency in
`../supervisor/README.md` was a claim about code that had never been asked to
do any of it.

## What it does, and the four operations it does

`MediaToolServiceImpl` implements the contract's `MediaToolService` root. It
admits only four exact operation, tool-id and preset shapes:

- `PROBE_MEDIA` / `media.probe` returns bounded media metadata.
- `EXTRACT_AUDIO` / `media.audio.extract` /
  `audio.wav.pcm16.v1` decodes the first supported audio stream into a
  canonical one- or two-channel PCM16 WAVE file.
- `SAMPLE_FRAMES` / `media.frames.sample` /
  `frames.png.zip.v1` writes bounded PNG samples and a content-free manifest
  into a deterministic ZIP.
- `TRANSCODE_PRESET` / `media.transcode` /
  `audio.wav.mono-pcm16.v1` decodes the first supported audio stream and
  downmixes it to a mono PCM16 WAVE file.

Every other operation, tool id or preset is refused before a descriptor is
read. The caller cannot supply a codec, container, filter or command line.

The probe returns five numbers — duration, audio and video stream counts, and
the largest video stream's width and height — and no string. That is the whole
security argument of the operation: a container's title, artist, encoder,
codec and container names are written by whoever produced the file, and a probe
that returned them would make an untrusted party the author of a value the
browser goes on to display. `MediaReading` in `media_probe.h` has nowhere for a
string to go, so the rule cannot be relaxed by accident.

`media_probe.cc` is a demuxer pass over Chromium's own `FFmpegDemuxer`, reading
a descriptor the browser opened and mapped. A transform receives that input
descriptor and a distinct broker-owned output descriptor. The worker receives
no path or URL, so a container that references an external file — a playlist,
a segmented stream — resolves to nothing rather than to whatever the sandbox
would otherwise have allowed.

The worker rechecks the contract bounds inside the process: input and output
are each at most 16 MiB, sampled video is at most 12 frames at 1920×1080,
media duration is at most ten minutes, and worker time is capped at 30 seconds.
Cancellation, deadline and resource failures return closed terminal statuses;
a failed transform truncates its output rather than leaving partial bytes.

## One job per process, which is where the parallelism is

There is no queue, no scheduler and no worker pool in this directory. A second
`Start` on a live process is `kBackpressure`, because a queue here would be a
scheduler the browser cannot see, cancel or bound.

Concurrency is the browser launching several of these:
`taffy-core/browser/profile_media_tool_launcher.{h,cc}` starts one process per
job and the supervisor's `kMaxConcurrentJobs` is the ceiling. That split is
what makes the bound mean something — a wedged parse, a hostile container that
drives the demuxer into an enormous allocation, and a worker killed for
exceeding its budget are all one process ending, and the browser learns about
each the same way.

`taffy-core/test/tool/media_tool_worker_browsertest.cc` is where that is
measured rather than described: eight jobs admitted before any has finished,
eight processes alive at that moment, and each returning the duration of its
own file. That browser test covers the probe, concurrent processes and
cancellation; it does not exercise the three transforms.

## Sandbox

`kService`, pinned in `taffy-core/contracts/tool-runtime/tool_runtime_service.mojom`
with the upstream precedent it was chosen against — Chromium's own media
gallery parser, which parses container metadata with the same pinned FFmpeg and
pins the same sandbox. Every operation here is software over a descriptor the
browser opened, so a wider sandbox would grant driver access no operation uses.

## What a test uses instead of a checked-in file

`test/synthetic_media.{h,cc}` builds a RIFF/WAVE container in memory. A
committed media file would be a binary under a source tree whose provenance
sweep would have to account for it, and its duration and stream count would be
facts a reader takes on trust; here they are arguments beside the assertion
about them. `media_tool_service_unittest.cc` additionally exercises audio
extraction, mono transcoding, deterministic frame sampling, closed preset
admission and descriptor authority. Those unit tests have run; the process
browser test has compiled and has not run.

## Commands

```bash
./tools/chromium/build --profile dev-arm64 taffy_unittests
./tools/chromium/test --profile dev-arm64 taffy_unittests
```

Decision `docs/decisions/0044-the-first-tool-worker.md` is the record.
