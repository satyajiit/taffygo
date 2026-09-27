// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useEffect, useReducer, useRef, useState } from "react";
import { Check, Circle, Globe, Hand, Pause, Play, RotateCcw } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import { demoSteps, initialSimulation, simulationReducer, type DemoKind } from "@/lib/simulation";
import { prepareDemoPhoto } from "@/lib/prepare-demo-photo";
import { GovernmentDemo } from "./GovernmentDemo";
import { BankingDemo, type DemoPhoto } from "./BankingDemo";

const scenarios = {
  document: { label: "Government document", request: "Download my government document.", site: "civic.example / records", description: "Follow the service pages, hand over verification, and save the PDF." },
  banking: { label: "Banking form", request: "Fill my banking form using my Library.", site: "bank.example / apply", description: "Fill saved details, resize the photo, and review before submitting." },
};

export function WorkflowDemo() {
  const [state, dispatch] = useReducer(simulationReducer, initialSimulation);
  const [photo, setPhoto] = useState<DemoPhoto>();
  const [photoError, setPhotoError] = useState<string>();
  const [retry, setRetry] = useState(0);
  const host = useRef<HTMLDivElement>(null);
  const inView = useRef(false);
  const steps = demoSteps[state.kind];
  const step = steps[state.step]!;
  const scenario = scenarios[state.kind];
  const done = state.step === steps.length - 1;
  const needsPhoto = state.kind === "banking" && state.step >= 3;

  useEffect(() => {
    if (!needsPhoto) return;
    const controller = new AbortController();
    let url: string | undefined;
    prepareDemoPhoto(withBasePath("/demo/sample-photo.png"), controller.signal).then(result => {
      url = URL.createObjectURL(result.blob);
      setPhoto({ ...result, url });
    }).catch(error => {
      if (!controller.signal.aborted) setPhotoError(error instanceof Error ? error.message : "The photo could not be prepared.");
    });
    return () => { controller.abort(); if (url) URL.revokeObjectURL(url); };
  }, [needsPhoto, retry]);

  useEffect(() => {
    if (!host.current || typeof IntersectionObserver === "undefined") return;
    const observer = new IntersectionObserver(entries => {
      inView.current = entries[0]?.isIntersecting ?? false;
      if (inView.current && !state.started && !window.matchMedia("(prefers-reduced-motion: reduce)").matches && document.documentElement.dataset.motionPaused !== "true") dispatch({ type: "play" });
    }, { threshold: 0.25 });
    observer.observe(host.current);
    return () => observer.disconnect();
  }, [state.kind, state.started]);

  useEffect(() => {
    if (!state.playing) return;
    const timer = window.setInterval(() => {
      if (document.hidden || document.documentElement.dataset.motionPaused === "true") return;
      if (typeof IntersectionObserver !== "undefined" && !inView.current) return;
      dispatch({ type: "tick", milliseconds: 100, fileReady: Boolean(photo) });
    }, 100);
    return () => window.clearInterval(timer);
  }, [state.playing, photo]);

  const resetPhoto = () => { setPhoto(undefined); setPhotoError(undefined); };
  return (
    <div className="workflow-demo" ref={host} data-playing={state.playing} data-kind={state.kind} data-step={state.step}>
      <div className="workflow-choices" role="group" aria-label="Choose a demo">
        {(Object.keys(scenarios) as DemoKind[]).map(kind => <button key={kind} aria-pressed={state.kind === kind} onClick={() => { resetPhoto(); dispatch({ type: "switch", kind }); }}>{scenarios[kind].label}</button>)}
      </div>
      <div className="simulation-layout">
        <div className="simulation-narrative">
          <span className="sim-label">Your request</span>
          <h3>“{scenario.request}”</h3>
          <p>{scenario.description}</p>
          <ol className="simulation-timeline">{steps.map((item, i) => <li key={item.title} aria-current={state.step === i ? "step" : undefined} data-complete={i < state.step}>
            <span>{i < state.step ? <Check size={14} /> : i === state.step && item.gate ? <Hand size={14} /> : <Circle size={8} fill={i === state.step ? "currentColor" : "none"} />}</span>
            <div><strong>{item.title}</strong>{state.step === i && <p>{item.detail}</p>}</div>
          </li>)}</ol>
          <div className="playback-controls">
            <button className="btn btn-ink" disabled={Boolean(step.gate)} onClick={() => {
              if (done) { resetPhoto(); dispatch({ type: "restart" }); }
              else dispatch({ type: state.playing ? "pause" : "play" });
            }}>{done ? <RotateCcw size={16} /> : state.playing ? <Pause size={16} /> : <Play size={16} />}{done ? "Replay demo" : state.playing ? "Pause demo" : step.gate ? "Waiting for you" : "Play demo"}</button>
            <button className="icon-button" aria-label="Restart demo" onClick={() => { resetPhoto(); dispatch({ type: "restart" }); }}><RotateCcw size={18} /></button>
            <span>{String(state.step + 1).padStart(2, "0")} / {String(steps.length).padStart(2, "0")}</span>
          </div>
        </div>
        <div className="simulation-stage">
          <div className="simulation-destination"><Globe size={15} /><span>{scenario.site}</span><span className="demo-label">Demo</span></div>
          {state.kind === "document" ? <GovernmentDemo state={state} onContinue={() => dispatch({ type: "consent" })} /> : <BankingDemo state={state} photo={photo} error={photoError} onRetry={() => { resetPhoto(); setRetry(retry + 1); }} onApprove={() => dispatch({ type: "consent" })} />}
          <div className="taffy-activity" role="status" aria-live="polite"><img src={withBasePath("/brand/taffygo-mark-color-on-light-180.webp")} width={26} height={26} alt="" /><span>{step.gate ? "Taffy is waiting for you" : done ? "Taffy finished the task" : state.playing ? step.title : "Demo paused"}</span>{state.playing && <span className="activity-dots" aria-hidden="true"><i /><i /><i /></span>}</div>
        </div>
      </div>
      <p className="workflow-notice">Demo with sample data. No external websites or accounts are used. The photo is resized locally in your browser.</p>
    </div>
  );
}
