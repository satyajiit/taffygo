// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useState } from "react";
import { ArrowLeft, ArrowUp, AudioLines, BookOpen, Check, Download, Globe, Grid2X2, Layers, Plus, Search, Settings, ShieldCheck, X } from "lucide-react";
import { withBasePath } from "@/lib/base-path";

const destinations = [
  { label: "Downloads", icon: Download }, { label: "Workspaces", icon: Grid2X2 },
  { label: "Tabs", icon: Layers }, { label: "Settings", icon: Settings },
];

/** The idle start page follows StartPageBody.kt; sites and files are sample data. */
export function NewTabPreview() {
  const [view, setView] = useState("New tab");
  const [query, setQuery] = useState("");
  const [intent, setIntent] = useState("");
  const [evening, setEvening] = useState(false);
  return <div className="ntp-shell" aria-label="Interactive new-tab preview">
    <div className="ntp-status"><span>9:41</span><span className="ntp-signal"><i /><i /><i /><span /></span></div>
    {view === "New tab" ? <div className={`ntp-body ${query ? "ntp-typing" : ""}`}>
      {!query && <>
        <button className="ntp-scene" onClick={() => setEvening(!evening)} aria-label={evening ? "Show morning scene" : "Show evening scene"}><img src={withBasePath(`/start-scenes/${evening ? "evening" : "morning"}-320.webp`)} width={320} height={240} alt={evening ? "The app’s evening scene: reading at home" : "The app’s morning scene: a bicycle by an open door"} /></button>
        <div className="ntp-brand"><img className="lockup lockup-light" src={withBasePath("/brand/taffygo-lockup-color-on-light-118@2x.webp")} width={118} height={59} alt="TaffyGo" /><img className="lockup lockup-dark" src={withBasePath("/brand/taffygo-lockup-color-on-dark-118@2x.webp")} width={118} height={59} alt="TaffyGo" /></div>
        <p className="ntp-greeting">Where to today?</p>
      </>}
      <form className="ntp-address" onSubmit={event => { event.preventDefault(); if (query) setIntent("Choose Search, Ask Taffy, or Task for Taffy below."); }}>
        <button type="button" aria-label="Open preview Library" onClick={() => setView("Library")}><Plus size={17} /></button><input value={query} onChange={event => { setQuery(event.target.value); setIntent(""); }} aria-label="Try the address box" placeholder="Search or ask Taffy" autoComplete="off" />{query ? <button type="button" aria-label="Clear the address box" onClick={() => { setQuery(""); setIntent(""); }}><X size={15} /></button> : <AudioLines size={17} aria-hidden="true" />}<button className="ntp-go" type="submit" aria-label="Use these words"><ArrowUp size={15} /></button>
      </form>
      {query ? <div className="ntp-intents">
        {[{label:"Task for Taffy",icon:Layers,body:"Let Taffy work through the steps"},{label:"Search",icon:Search,body:"Look for pages on the web"},{label:"Ask Taffy",icon:BookOpen,body:"Ask a question about a page"}].map(({label,icon:Icon,body}) => <button key={label} onClick={() => setIntent(`${label} selected. This is a preview; open TaffyGo to continue.`)}><Icon size={18} /><span><strong>{label}</strong><small>{body}</small></span></button>)}
        {intent && <p className="ntp-feedback" role="status">{intent}</p>}
      </div> : <div className="ntp-sites" aria-label="Sample frequent sites">{[{name:"Wikipedia",mark:"W",href:"https://www.wikipedia.org"},{name:"GitHub",mark:"GH",href:"https://github.com"},{name:"YouTube",mark:"▶",href:"https://www.youtube.com"},{name:"Read later",mark:"+",href:"#browser-tools"}].map(site=><a key={site.name} href={site.href}><span>{site.mark}</span>{site.name}</a>)}</div>}
    </div> : <div className="ntp-subview"><button className="ntp-back" onClick={() => setView("New tab")}><ArrowLeft size={17} /> New tab</button><h2>{view}</h2>
      {view === "Downloads" ? <><div className="ntp-file"><Download size={22} /><span>Residence certificate<small>Sample PDF · Saved</small></span><Check size={15} /></div><a className="text-link" href={withBasePath("/demo/residence-sample.pdf")} download>Open sample PDF</a></> : view === "Settings" ? <><div className="ntp-file"><ShieldCheck size={21} /><span>Ad and tracker blocking<small>On by default</small></span><Check size={15} /></div><button className="ntp-file" onClick={() => { setEvening(!evening); setView("New tab"); }}><Globe size={21} /><span>Start-page scene<small>Try {evening ? "morning" : "evening"}</small></span></button></> : <><div className="ntp-mini-tabs"><div><BookOpen size={28} /><strong>Reading list</strong><small>3 saved pages</small></div><div><Layers size={28} /><strong>Weekend plans</strong><small>2 sources</small></div></div><p className="ntp-subnote">Sample {view.toLowerCase()} in this preview.</p><a className="text-link" href={withBasePath("/#browser-tools")}>Explore browser features</a></>}
    </div>}
    <div className="ntp-dock" role="group" aria-label="Preview browser controls">{destinations.map(({label,icon:Icon})=><button key={label} aria-label={`Preview ${label.toLowerCase()}`} aria-pressed={view===label} onClick={()=>{setView(view===label ? "New tab" : label); setIntent("");}}><Icon size={19} />{label === "Tabs" && <span>3</span>}</button>)}</div>
  </div>;
}
