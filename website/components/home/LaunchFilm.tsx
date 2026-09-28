// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useRef, useState, useSyncExternalStore } from "react";
import { ArrowDownToLine, LoaderCircle, Play, RotateCcw } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import { launchFilm } from "@/lib/content/launch-film";

const subscribeToHydration = () => () => {};

export function LaunchFilm() {
  const player = useRef<HTMLVideoElement>(null);
  const hydrated = useSyncExternalStore(subscribeToHydration, () => true, () => false);
  const [status, setStatus] = useState<"idle" | "loading" | "playing" | "paused" | "ended" | "error">("idle");
  const [hasPlayed, setHasPlayed] = useState(false);
  const showCover = !hasPlayed || status === "ended" || status === "error";
  const busy = status === "loading";
  const filmUrl = withBasePath(launchFilm.video);

  async function playFilm() {
    const video = player.current;
    if (!video) return;
    if (status === "error") video.load();
    if (video.ended) video.currentTime = 0;
    video.muted = false;
    setStatus("loading");
    try {
      await video.play();
      video.focus({ preventScroll: true });
    } catch {
      setStatus("error");
    }
  }

  return (
    <section id="launch-film" className="container-site launch-film" aria-labelledby="launch-film-title">
      <div className="launch-film__heading">
        <h2 id="launch-film-title">Watch TaffyGo work.</h2>
        <p id="launch-film-description">From everyday browsing to getting things done.<br />The launch film, in under two minutes.</p>
      </div>
      <figure className="launch-film__figure">
        <div className="launch-film__stage" data-status={status} aria-busy={busy}>
          <video ref={player} controls={!hydrated || !showCover} playsInline preload="none" width={3840} height={2160}
            poster={withBasePath(launchFilm.poster)} tabIndex={0}
            aria-label="TaffyGo launch film" aria-describedby="launch-film-description"
            onPlaying={() => { setHasPlayed(true); setStatus("playing"); }}
            onPause={() => setStatus("paused")} onWaiting={() => setStatus("loading")}
            onEnded={() => setStatus("ended")} onError={() => setStatus("error")}>
            <source src={filmUrl} type="video/webm" />
            <track kind="captions" src={withBasePath(launchFilm.captions)} srcLang="en" label="English" />
            <a href={filmUrl}>Watch the TaffyGo launch film</a>
          </video>
          {showCover && <button type="button" className="launch-film__cover" onClick={playFilm} disabled={busy}
            aria-label={status === "ended" ? "Watch the film again" : status === "error" ? "Retry playing the film" : "Play the launch film with sound"}>
            <span className="launch-film__play">
              {busy ? <LoaderCircle aria-hidden="true" className="launch-film__spinner" /> : status === "ended" ? <RotateCcw aria-hidden="true" /> : <Play aria-hidden="true" fill="currentColor" />}
              <span>{busy ? "Loading film…" : status === "ended" ? "Watch again" : status === "error" ? "Try again" : "Watch the film"}</span>
              <span className="launch-film__duration">1:58</span>
            </span>
          </button>}
          <noscript><style>{".launch-film__cover{display:none}"}</style></noscript>
        </div>
        <figcaption className="launch-film__caption">
          <span>Launch film <span aria-hidden="true">/</span> 4K · 60 fps <span aria-hidden="true">/</span> Sound on</span>
          <a href={filmUrl} download><ArrowDownToLine size={16} aria-hidden="true" /> Download film</a>
        </figcaption>
      </figure>
      {status === "error" && <p className="launch-film__error" role="alert">The film couldn’t load. Try again or <a href={filmUrl}>open the video directly</a>.</p>}
      <details className="launch-film__transcript">
        <summary>Read the transcript</summary>
        <div>{launchFilm.transcript.map((paragraph) => <p key={paragraph}>{paragraph}</p>)}</div>
      </details>
    </section>
  );
}
