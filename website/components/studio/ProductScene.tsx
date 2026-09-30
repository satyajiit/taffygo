// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowDown, Check, ShieldCheck } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import { MotionControl } from "./MotionControl";
import { NewTabPreview } from "./NewTabPreview";

export function ProductScene() {
  return <div className="newtab-stage">
    <div className="newtab-orbit" aria-hidden="true" />
    <div className="newtab-device"><NewTabPreview /></div>
    <div className="hero-task-peek"><span className="peek-title"><img src={withBasePath("/brand/taffygo-mark-color-on-light-180.webp")} width={22} height={22} alt="" /> Task for Taffy</span><strong>Plan a weekend in Pune</strong><span><Check size={13} /> Find places to visit</span><span><Check size={13} /> Compare opening hours</span><span className="peek-current"><span /> Put the plan together</span><small>Example task</small></div>
    <div className="hero-block-peek"><ShieldCheck size={20} /><span>Ad blocking<strong>On from the start</strong></span></div>
    <img className="hero-taffy-small" src={withBasePath("/cutouts/taffy-240.webp")} srcSet={`${withBasePath("/cutouts/taffy-240.webp")} 240w, ${withBasePath("/cutouts/taffy-360.webp")} 360w`} sizes="(min-width: 480px) 140px, 104px" width={360} height={540} alt="Taffy, the browser’s winged assistant" />
    <div className="newtab-stage-caption"><span><ArrowDown size={13} /> Try the address box and dock</span><MotionControl /></div>
    <p className="newtab-preview-note">Interactive app preview · sample sites and tasks</p>
  </div>;
}
