// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { ArrowRight, Check, CheckCheck, FileImage, Library, LoaderCircle, MoveDown, Send, Upload } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import { typedValue, type SimulationState } from "@/lib/simulation";
import type { PreparedPhoto } from "@/lib/prepare-demo-photo";

export type DemoPhoto = PreparedPhoto & { url: string };
const fields = [{ label: "Full name", value: "Samira Rao" }, { label: "Email", value: "samira@example.com" }, { label: "City", value: "Pune" }];

export function BankingDemo({ state, photo, error, onRetry, onApprove }: {
  state: SimulationState; photo?: DemoPhoto; error?: string; onRetry: () => void; onApprove: () => void;
}) {
  const { step, elapsed } = state;
  if (step === 3) return (
    <div className="sim-page compression-page" key="compress">
      <div className="compression-header"><FileImage size={22} /><span>Photo requirement<strong>Under 200 KB</strong></span></div>
      <div className="photo-comparison">
        <figure><img src={withBasePath("/demo/sample-photo.png")} width={1254} height={1254} alt="Sample application photo before resizing" /><figcaption>Original<span>{photo ? `${(photo.originalBytes / 1_000_000).toFixed(1)} MB` : "Loading photo…"}</span></figcaption></figure>
        <MoveDown className="compression-arrow" size={22} />
        <figure data-ready={Boolean(photo)}>{photo ? <img src={photo.url} width={photo.width} height={photo.height} alt="Resized application photo" /> : <div className="photo-loading"><LoaderCircle size={24} className="spin" /></div>}<figcaption>Prepared copy<span>{photo ? `${Math.ceil(photo.blob.size / 1000)} KB · ${photo.width} × ${photo.height}` : "Resizing…"}</span></figcaption></figure>
      </div>
      <div className="transfer-track"><span style={{ transform: `scaleX(${photo ? Math.min(elapsed / 2800, 1) : 0.35})` }} /></div>
      {error ? <div className="sim-error" role="alert"><p>{error}</p><button className="btn btn-outline" onClick={onRetry}>Retry photo</button></div> : <p className="compression-result">{photo ? <><Check size={17} /> Under 200 KB. Original kept.</> : "Resizing the sample photo in your browser."}</p>}
    </div>
  );
  if (step >= 6) return (
    <div className="sim-page sim-handover" key="confirmation">
      <span className="sim-large-icon">{step === 7 ? <CheckCheck size={34} /> : <Send size={30} />}</span>
      <h4>{step === 7 ? "Application received" : "Submitting the application…"}</h4>
      <p>{step === 7 ? "Reference DEMO-2048" : "Sending the details and prepared photo you approved."}</p>
      {step === 7 ? <div className="confirmation-detail"><Check size={17} /><span>Details submitted<br /><small>Photo accepted · Under 200 KB</small></span></div> : <div className="transfer-track"><span style={{ transform: `scaleX(${elapsed / 2000})` }} /></div>}
      <small>Demo confirmation. No bank receives this data.</small>
    </div>
  );
  return (
    <div className="sim-page banking-page" key="form">
      <div className="portal-brand"><span className="bank-mark">B</span><span>Bank application<span>Sample form</span></span><span className="sim-small">1 of 1</span></div>
      {step === 1 && <div className="library-transfer"><Library size={18} /><span>Reading selected Library details</span><Check size={16} /></div>}
      <h4>{step === 5 ? "Review your application" : "Your details"}</h4>
      <div className="demo-form-fields">
        {fields.map((field, i) => <label key={field.label}>{field.label}<span className="demo-filled-field" data-filling={step === 2 && elapsed > i * 1050 && elapsed < (i + 1) * 1050}>{step > 2 ? field.value : step === 2 ? typedValue(field.value, elapsed, i * 1050) : <span className="field-placeholder">{field.label}</span>}{step > 2 && <Check size={14} />}</span></label>)}
      </div>
      <div className="demo-attachment" data-attached={step >= 4}>
        {step >= 4 && photo ? <img src={photo.url} width={48} height={48} alt="Attached sample photo" /> : <Upload size={22} />}
        <span><strong>{step >= 4 ? "application-photo.jpg" : "Upload a photo"}</strong><small>{step >= 4 && photo ? `${Math.ceil(photo.blob.size / 1000)} KB · Ready` : "JPEG · Maximum 200 KB"}</small></span>{step >= 4 && <Check size={18} />}
      </div>
      {step === 5 ? <button className="btn btn-ink approval-button" onClick={onApprove}>Approve submission <ArrowRight size={16} /></button> : <p className="sim-small">{step === 0 ? "Reading the form requirements" : "Taffy will ask before submitting."}</p>}
    </div>
  );
}
