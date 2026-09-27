// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";
import { useState } from "react";
import { Archive, ArrowUpRight, BookOpen, Check, Download, FileImage, FileText, Folder, Globe, Layers, LockKeyhole, Plus, ShieldCheck, Sun } from "lucide-react";
import { withBasePath } from "@/lib/base-path";

const panels = [
  { id: "tabs", label: "Tabs", icon: Layers, title: "Your tabs. Taffy’s tabs.", body: "Taffy’s task pages stay in their own group. Switch back to your pages whenever you want, or open a private tab from the tab switcher." },
  { id: "library", label: "Library", icon: BookOpen, title: "Keep what you want to use again.", body: "Organize saved pages and files in your Library. Choose the details and documents a task can use, then review the result." },
  { id: "blocking", label: "Ad blocking", icon: ShieldCheck, title: "Block requests before they load.", body: "Ad and tracker blocking is built in. See what was blocked and use the per-site switch when a page needs an exception." },
  { id: "backup", label: "Backups", icon: Archive, title: "A backup file you keep.", body: "Create an encrypted backup and store it where you choose. Restore from that file when you need it, without a TaffyGo cloud account." },
];

function TabsPreview() {
  const [privateTabs, setPrivateTabs] = useState(false);
  const [added, setAdded] = useState(false);
  return <div className="tabs-preview"><div className="preview-segments" role="group" aria-label="Preview tab groups"><button aria-pressed={!privateTabs} onClick={() => setPrivateTabs(false)}>Your tabs <span>3</span></button><button aria-pressed={privateTabs} onClick={() => setPrivateTabs(true)}><LockKeyhole size={14} /> Private</button></div>
    {privateTabs ? <div className="private-preview"><LockKeyhole size={32} /><h4>{added ? "New private tab" : "Private tabs"}</h4><p>{added ? "Your private tab is open in this preview." : "Private tabs have a separate place in the switcher."}</p><button className="btn btn-outline" onClick={() => setAdded(!added)}>{added ? "Close sample tab" : "Open sample tab"}</button></div> : <><div className="mini-tab-grid"><div className="mini-tab"><div className="mini-tab-page recipe-tile"><span className="recipe-sun"><Sun size={26} /></span><strong>A weekend<br />in Pune</strong><i /><i /></div><span><Globe size={13} /> Weekend plans</span></div><div className="mini-tab"><div className="mini-tab-page reading-tile"><BookOpen size={28} /><strong>The reading list</strong><i /><i /><i /></div><span><BookOpen size={13} /> Saved reading</span></div><div className="mini-tab"><div className="mini-tab-page code-tile"><img src={withBasePath("/providers/github.svg")} className="mono-logo" width={30} height={30} alt="" /><strong>taffygo</strong><span>Code · Issues · Discussions</span><i /><i /></div><span><Globe size={13} /> GitHub</span></div></div><div className="taffy-tabs-row"><img src={withBasePath("/brand/taffygo-mark-color-on-light-180.webp")} width={24} height={24} alt="" /><span>Taffy’s tabs <strong>2</strong></span><span className="small-task-dot" /> Working on your task</div></>}
  </div>;
}

function LibraryPreview() {
  const [selected, setSelected] = useState("Travel notes");
  return <div className="library-preview"><div className="library-sidebar"><strong><Folder size={16} /> Collections</strong><span className="active">All saved items <b>12</b></span><span>Personal <b>4</b></span><span>Travel <b>5</b></span><span>Reading <b>3</b></span></div><div className="library-files">{[{title:"Travel notes",type:"Saved page",icon:BookOpen},{title:"Application photo",type:"Image · 27 KB",icon:FileImage},{title:"Address details",type:"Selected details",icon:FileText}].map(({title,type,icon:Icon})=><button key={title} aria-pressed={selected===title} onClick={()=>setSelected(title)}><Icon size={24} /><strong>{title}</strong><small>{type}</small>{selected===title&&<Check size={15} />}</button>)}<div className="library-selection" role="status"><Check size={16} /><span>{selected} selected for this preview.</span></div></div></div>;
}

function BlockingPreview() {
  const [enabled,setEnabled]=useState(true);
  return <div className="blocking-preview"><div className="blocking-domain"><Globe size={18} /><span>example.com</span><span>Sample page</span></div><div className="blocking-switch"><div><ShieldCheck size={28} /><strong>Ad and tracker blocking</strong></div><button role="switch" aria-checked={enabled} aria-label="Preview ad blocking" onClick={()=>setEnabled(!enabled)}><span /></button></div><div className="blocking-count"><strong>{enabled ? "12" : "0"}</strong><span>{enabled ? "requests blocked in this example" : "blocking paused for this example"}</span></div><div className="request-rows">{["Advertising requests","Tracking scripts","Page content"].map((text,i)=><div key={text}><span>{text}</span><strong data-blocked={i<2&&enabled}>{i<2&&enabled ? "Blocked" : "Allowed"}</strong></div>)}</div></div>;
}

function BackupPreview() {
  const [created,setCreated]=useState(false);
  return <div className="backup-preview"><span className="backup-symbol"><Archive size={38} /><LockKeyhole size={20} /></span><h4>{created ? "Your backup file" : "Create a backup file"}</h4><p>Bookmarks, settings, and saved work.<br />One encrypted file, kept by you.</p>{created&&<div className="backup-file"><FileText size={23} /><span>taffygo-backup.taffy<small>Sample file preview</small></span><Check size={17} /></div>}<button className="btn btn-outline" onClick={()=>setCreated(!created)}>{created ? "Reset preview" : "Show backup preview"}<Download size={15} /></button></div>;
}

export function BrowserWorkbench() {
  const [selected,setSelected]=useState("tabs");
  const panel=panels.find(item=>item.id===selected)!;
  return <section id="browser-tools" className="container-site workbench-section" aria-labelledby="workbench-title"><div className="section-intro"><h2 id="workbench-title">Tabs, files,<br />and site controls.</h2><p>Browse, save pages, manage files, and pick up where you left off. These controls are part of the browser, with or without an AI connection.</p></div><div className="workbench-tabs" role="group" aria-label="Browser features">{panels.map(({id,label,icon:Icon})=><button key={id} id={`feature-${id}`} aria-controls="browser-feature-panel" aria-pressed={selected===id} onClick={()=>setSelected(id)}><Icon size={18} />{label}</button>)}</div><div className="workbench-content" role="region" id="browser-feature-panel" aria-labelledby={`feature-${selected}`}><div className="workbench-explanation"><h3>{panel.title}</h3><p>{panel.body}</p><a className="text-link" href={withBasePath("/built-for-phones/")}>See the app screens <ArrowUpRight size={16} /></a><small>Interactive preview with sample data.</small></div><div className="workbench-canvas" key={selected}><div className="preview-window-bar"><span><i /><i /><i /></span><strong>{panel.label}</strong><Plus size={14} /></div>{selected==="tabs"?<TabsPreview />:selected==="library"?<LibraryPreview />:selected==="blocking"?<BlockingPreview />:<BackupPreview />}</div></div></section>;
}
