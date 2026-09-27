// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { useState } from "react";
import { ArrowDownToLine, ArrowRight, Check, FileText, FolderOpen, Hand, Landmark, LockKeyhole, MousePointer2, ShieldCheck } from "lucide-react";
import { withBasePath } from "@/lib/base-path";
import type { SimulationState } from "@/lib/simulation";

function VerificationGate({ onContinue }: { onContinue: () => void }) {
  const [verified, setVerified] = useState(false);
  return (
    <div className="sim-page sim-handover" key="verify">
      <span className="sim-large-icon"><Hand size={32} /></span>
      <h4>The website needs you</h4>
      <p>Complete the CAPTCHA or sign-in on the page. Taffy will wait.</p>
      <label className="verification-check"><input type="checkbox" checked={verified} onChange={e => setVerified(e.target.checked)} /><span>I’ve completed verification</span></label>
      <button className="btn btn-ink" disabled={!verified} onClick={onContinue}>Hand back to Taffy <ArrowRight size={16} /></button>
      <small>This demo uses a checkbox in place of a real CAPTCHA.</small>
    </div>
  );
}

export function GovernmentDemo({ state, onContinue }: { state: SimulationState; onContinue: () => void }) {
  const { step, elapsed } = state;
  if (step === 2) return <VerificationGate onContinue={onContinue} />;
  if (step >= 4) return (
    <div className="sim-page sim-download" key="download">
      <img className="document-cutout" src={withBasePath("/cutouts/document.webp")} width={320} height={320} alt="" />
      <h4>{step === 5 ? "Your document is saved" : "Downloading your document…"}</h4>
      <p>Residence certificate · PDF</p>
      <div className="transfer-track"><span style={{ transform: `scaleX(${step === 5 ? 1 : elapsed / 2200})` }} /></div>
      {step === 5 ? <a className="btn btn-ink" href={withBasePath("/demo/residence-sample.pdf")} download><ArrowDownToLine size={16} /> Download sample PDF</a> : <span className="sim-small">Saving to Downloads</span>}
      <small>Sample document for this demo.</small>
    </div>
  );
  return (
    <div className="sim-page sim-government" key={step === 0 ? "search" : "records"}>
      <div className="portal-brand"><Landmark size={24} /><span>Public records<span>Sample government service</span></span><ShieldCheck size={20} /></div>
      {step === 0 ? <>
        <h4>Find a document service</h4>
        <div className="sim-search-query">Residence certificate<span className="typing-caret" /></div>
        <div className="portal-result" data-selected={elapsed > 1100}><span><LockKeyhole size={13} /> civic.example</span><strong>Residence certificates & records</strong><p>View and download documents from your records.</p><ArrowRight size={20} /></div>
        <div className="portal-result subdued"><span>Information</span><strong>How to request a certificate</strong></div>
      </> : <>
        <h4>{step === 3 ? "Your documents" : "Document services"}</h4>
        <p>{step === 3 ? "Signed in · Sample account" : "Choose the service you need."}</p>
        {["Residence certificate", "Registration record", "Application status"].map((title, i) => <div key={title} className="portal-record" data-selected={i === 0 && elapsed > 800}><FileText size={20} /><span><strong>{title}</strong><small>{step === 3 && i === 0 ? "Ready to download · PDF" : "View record"}</small></span>{step === 3 && i === 0 ? <ArrowDownToLine size={19} /> : <FolderOpen size={19} />}</div>)}
      </>}
      <MousePointer2 className="sim-cursor" size={28} fill="currentColor" aria-hidden="true" />
      <div className="sim-action"><Check size={15} />{step === 0 ? "Checking the service address" : step === 1 ? "Opening the document request" : "Document found"}</div>
    </div>
  );
}
