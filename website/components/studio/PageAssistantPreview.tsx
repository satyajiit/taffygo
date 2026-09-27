// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";
import { useState } from "react";
import { ArrowRight, ArrowUpRight, Carrot, Check, ChefHat, Clock3, Leaf, Link2, Soup } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
const questions = [
  {label:"How long does it take?",answer:"25 minutes: 10 minutes to prepare and 15 minutes to cook.",source:"Recipe timing",sourceId:"recipe-timing",icon:Clock3},
  {label:"What do I need?",answer:"Carrots, red lentils, onion, vegetable stock, and fresh herbs. The recipe serves two.",source:"Ingredients",sourceId:"recipe-ingredients",icon:Carrot},
  {label:"What’s the first step?",answer:"Chop the carrots and onion, then soften them in a saucepan before adding the lentils and stock.",source:"Method, step 1",sourceId:"recipe-method",icon:ChefHat},
];
export function PageAssistantPreview() {
  const [selected,setSelected]=useState(0);
  const question=questions[selected]!;
  return <section className="page-help-section" aria-labelledby="page-help-title"><div className="container-site"><div className="section-intro"><h2 id="page-help-title">Ask the page.<br />Keep the source.</h2><p>Get a summary, pull out a detail, or check a step while the page stays open. Taffy’s answer links back to the information it used.</p></div><div className="page-help-demo"><div className="recipe-preview"><div className="recipe-address"><span /> recipes.example / carrot-soup <Link2 size={13} /></div><div className="recipe-art"><Soup size={76} strokeWidth={1} /><span><Carrot size={25} /><Leaf size={24} /></span><small>THE WEEKDAY KITCHEN</small></div><h3>Carrot & lentil soup</h3><div id="recipe-timing" className="recipe-timing"><span><Clock3 size={15} /> 25 minutes</span><span>Prep 10 min · Cook 15 min</span></div><div id="recipe-ingredients" className="recipe-ingredients"><strong>For the saucepan · serves 2</strong><span>Carrots <b>2</b></span><span>Red lentils <b>100 g</b></span><span>Onion <b>1</b></span><span>Vegetable stock <b>500 ml</b></span><span>Fresh herbs <b>To finish</b></span></div><p id="recipe-method" className="recipe-method"><b>01</b> Chop the carrots and onion. Soften in a saucepan, then add the lentils and stock.</p></div><div className="ask-preview"><div className="ask-preview-heading"><img src={withBasePath("/brand/taffygo-mark-color-on-light-180.webp")} width={30} height={30} alt="" /><strong>Ask Taffy</strong><span>This page</span></div><p className="ask-caption">Try a question</p><div className="ask-options">{questions.map(({label,icon:Icon},i)=><button key={label} aria-pressed={selected===i} onClick={()=>setSelected(i)}><Icon size={16} />{label}<ArrowRight size={15} /></button>)}</div><div className="ask-response" key={selected} role="status"><span><Check size={14} /> From the page</span><p>{question.answer}</p><a href={`#${question.sourceId}`}><Link2 size={13} /> {question.source}</a></div><span className="ask-source-note">Sample recipe and answers for this preview.</span></div></div><a className="text-link section-end-link" href={withBasePath("/product/")}>Follow a task in TaffyGo <ArrowUpRight size={16} /></a></div></section>;
}
