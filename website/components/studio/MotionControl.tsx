// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useEffect, useState } from "react";
import { Pause, Play } from "lucide-react";

export function MotionControl() {
  const [paused, setPaused] = useState(false);
  useEffect(() => {
    const items = document.querySelectorAll<HTMLElement>("[data-reveal]");
    if (typeof IntersectionObserver === "undefined" || window.matchMedia?.("(prefers-reduced-motion: reduce)").matches) return;
    const observer = new IntersectionObserver(entries => {
      for (const entry of entries) if (entry.isIntersecting) {
        (entry.target as HTMLElement).dataset.visible = "true";
        observer.unobserve(entry.target);
      }
    }, { threshold: 0.1 });
    for (const item of items) { item.dataset.revealReady = "true"; observer.observe(item); }
    return () => observer.disconnect();
  }, []);
  return <button className="motion-control" aria-pressed={paused} onClick={() => {
    const next = !paused;
    setPaused(next);
    document.documentElement.dataset.motionPaused = String(next);
    window.dispatchEvent(new Event("taffy-motion"));
  }}>{paused ? <Play size={13} /> : <Pause size={13} />}{paused ? "Play motion" : "Pause motion"}</button>;
}
