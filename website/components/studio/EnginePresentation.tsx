// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useEffect, useState } from "react";
import { ArrowRight, ArrowUpRight, Check, Play, ShieldCheck } from "lucide-react";
import { withBasePath } from "@/lib/base-path";

const layers = [
  { name: "Android interface", logo: "android", title: "Controls built with Compose", body: "Android handles the interface, file picker, and other system controls. Your tabs and Taffy’s task tabs stay separate.", tags: ["Tabs", "Files", "Controls"] },
  { name: "Chromium", logo: "chromium", title: "The browser that opens the web", body: "Chromium renders pages and handles navigation and downloads. TaffyGo blocks matching ad and tracker requests before they load.", tags: ["Pages", "Navigation", "Downloads"] },
  { name: "Rust service", logo: "rust", title: "Task logic in a separate process", body: "Rust runs task planning, permission checks, and model routing inside a sandboxed service. The browser handles access to the web and Android.", tags: ["Tasks", "Permissions", "Model routing"] },
  { name: "On-device Python", logo: "python", title: "Python is built into the app", body: "A separate worker embeds CPython for registered document and spreadsheet tools. It accepts structured inputs, with limits on time, memory, and output. The Android build includes it; phone-level execution checks remain pending.", tags: ["Documents", "Spreadsheets", "Local tools"] },
];
const traceLabels = ["Your request", "Permission check", "Browser action", "Result returned"];

export function EnginePresentation() {
  const [selected, setSelected] = useState(2);
  const [trace, setTrace] = useState(-1);
  useEffect(() => {
    if (trace < 0 || trace >= 3) return;
    const timer = window.setInterval(() => {
      if (!document.hidden && document.documentElement.dataset.motionPaused !== "true") setTrace(trace + 1);
    }, 1200);
    return () => window.clearInterval(timer);
  }, [trace]);
  const layer = layers[selected]!;
  return (
    <section className="engine-section" aria-labelledby="engine-title">
      <div className="container-site engine-grid">
        <div className="engine-copy" data-reveal>
          <h2 id="engine-title">Chromium. Rust.<br />Python on your phone.</h2>
          <p>Web pages, task logic, and local tools each have their own process. Select a layer to see its job.</p>
          <div className="engine-selector" role="group" aria-label="Browser architecture">
            {layers.map((item, i) => <button key={item.name} aria-pressed={selected === i} onClick={() => setSelected(i)}><img className={item.logo === "rust" ? "mono-logo" : ""} src={withBasePath(`/providers/${item.logo}.svg`)} width={22} height={22} alt="" />{item.name}</button>)}
          </div>
          <div className="engine-detail" key={selected} aria-live="polite"><h3>{layer.title}</h3><p>{layer.body}</p></div>
          <a href={withBasePath("/technology/")} className="text-link">Read about the architecture <ArrowUpRight size={17} /></a>
        </div>
        <div className="software-diagram" data-selected={selected} data-trace={trace}>
          <div className="software-stack" aria-label="Android interface and Chromium with separate Rust and Python services">
            {layers.map((item, i) => <button className={`software-layer software-layer-${i}`} key={item.name} aria-pressed={selected === i} aria-label={`Inspect ${item.name}`} onClick={() => setSelected(i)}>
              <span className="layer-heading"><img className={item.logo === "rust" ? "mono-logo" : ""} src={withBasePath(`/providers/${item.logo}.svg`)} width={26} height={26} alt="" /><strong>{item.name}</strong>{selected === i && <span className="layer-selected"><Check size={13} /></span>}</span>
              <span className="layer-parts">{item.tags.map(tag => <span key={tag}>{tag}</span>)}</span>
              {i >= 2 && <span className="sandbox-label"><ShieldCheck size={12} /> Separate process</span>}
            </button>)}
          </div>
          <div className="request-trace">
            <ol aria-label="Request path">{traceLabels.map((label, i) => <li key={label} aria-current={trace === i ? "step" : undefined} data-complete={trace > i}><span>{i + 1}</span>{label}{i < 3 && <ArrowRight size={12} />}</li>)}</ol>
            <button className="text-link" disabled={trace >= 0 && trace < 3} onClick={() => setTrace(0)}><Play size={14} />{trace >= 0 && trace < 3 ? "Following the request…" : "Trace a request"}</button>
          </div>
        </div>
      </div>
    </section>
  );
}
