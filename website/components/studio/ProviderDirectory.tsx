// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

"use client";

import { useDeferredValue, useState } from "react";
import { ArrowDownWideNarrow, Check, ChevronDown, Image as ImageIcon, ListFilter, Search, Wrench, X, Brain } from "lucide-react";
import directory from "@/lib/provider-directory.json";
import { providerIcon } from "@/lib/provider-icons";
import { withBasePath } from "@/lib/base-path";

type Provider = (typeof directory.providers)[number];
type Capability = "vision" | "reasoning" | "tools";
const capabilities = [{ key: "vision", label: "Images", icon: ImageIcon }, { key: "reasoning", label: "Reasoning", icon: Brain }, { key: "tools", label: "Tools", icon: Wrench }] as const;
const popular = ["OpenAI", "Anthropic", "Google", "DeepSeek", "OpenRouter"];

function ProviderEntry({ provider, expanded }: { provider: Provider; expanded: boolean }) {
  const logo = providerIcon(provider.id);
  return (
    <details className="provider-row" open={expanded ? true : undefined}>
      <summary>
        <span className="provider-logo">{logo ? <img className="provider-brand" data-provider={provider.id} src={withBasePath(logo)} width={30} height={30} alt="" loading="lazy" /> : <span>{provider.name.slice(0, 2)}</span>}</span>
        <span className="provider-name">{provider.name}<small>{provider.listing ? "Live model listing" : "Included catalog"}</small></span>
        <span className="provider-count">{provider.models.length}<span> models</span></span>
        <ChevronDown size={17} />
      </summary>
      <div className="model-table-head" aria-hidden="true"><span>Model</span><span>Capabilities / context</span></div>
      <ul className="model-list">
        {provider.models.map(model => <li key={model.id}>
          <span><strong>{model.name}</strong><small>{model.id}</small></span>
          <span className="model-capabilities">
            {!model.enabled && <span className="unavailable-model">Unavailable</span>}
            {model.vision && <span><ImageIcon size={12} /> Images</span>}
            {model.reasoning && <span><Brain size={12} /> Reasoning</span>}
            {model.tools && <span><Wrench size={12} /> Tools</span>}
            <span className="context-size">{Intl.NumberFormat("en", { notation: "compact" }).format(model.context)} context</span>
          </span>
        </li>)}
      </ul>
      {provider.models.length === 0 && <p className="provider-empty">Connect in TaffyGo to load this provider’s available models.</p>}
    </details>
  );
}

export function ProviderDirectory() {
  const [query, setQuery] = useState("");
  const [selected, setSelected] = useState<Capability[]>([]);
  const [availableOnly, setAvailableOnly] = useState(false);
  const [sort, setSort] = useState("name");
  const deferredQuery = useDeferredValue(query.trim().toLowerCase());
  const filtering = Boolean(deferredQuery) || selected.length > 0 || availableOnly;
  const filtered = directory.providers.map(provider => ({
    ...provider,
    models: provider.models.filter(model => `${provider.name} ${provider.id} ${model.name} ${model.id}`.toLowerCase().includes(deferredQuery)
      && selected.every(capability => model[capability]) && (!availableOnly || model.enabled)),
  })).filter(provider => provider.models.length > 0 || (!filtering && provider.listing));
  filtered.sort((a, b) => sort === "models" ? b.models.length - a.models.length || a.name.localeCompare(b.name) : a.name.localeCompare(b.name));
  const count = filtered.reduce((total, provider) => total + provider.models.length, 0);
  const reset = () => { setQuery(""); setSelected([]); setAvailableOnly(false); };
  return (
    <div className="provider-directory">
      <div className="directory-toolbar">
        <label className="directory-search" htmlFor="model-search"><span>Search providers and models</span><span className="search-field"><Search size={20} /><input id="model-search" value={query} onChange={event => setQuery(event.target.value)} placeholder="Search Claude, Gemini, DeepSeek…" type="search" autoComplete="off" />{query && <button aria-label="Clear search" onClick={() => setQuery("")}><X size={17} /></button>}</span></label>
        <label className="directory-sort" htmlFor="model-sort"><span>Sort by</span><span><ArrowDownWideNarrow size={17} /><select id="model-sort" value={sort} onChange={e => setSort(e.target.value)}><option value="name">Provider A–Z</option><option value="models">Most models</option></select><ChevronDown size={14} /></span></label>
      </div>
      <div className="directory-filters">
        <span className="filter-label"><ListFilter size={16} /> Capabilities</span>
        <div className="capability-chips" role="group" aria-label="Filter model capabilities">{capabilities.map(({ key, label, icon: Icon }) => <button key={key} aria-pressed={selected.includes(key)} onClick={() => setSelected(selected.includes(key) ? selected.filter(value => value !== key) : [...selected, key])}><Icon size={15} />{label}{selected.includes(key) && <Check size={13} />}</button>)}</div>
        <label className="available-filter"><input type="checkbox" checked={availableOnly} onChange={e => setAvailableOnly(e.target.checked)} /><span>Available only</span></label>
      </div>
      <div className="popular-providers"><span>Quick search</span>{popular.map(name => <button key={name} aria-pressed={query === name} onClick={() => setQuery(query === name ? "" : name)}>{name}</button>)}</div>
      <div className="directory-results-heading"><p className="directory-count" role="status" aria-live="polite"><strong>{count}</strong> model entries across <strong>{filtered.length}</strong> providers</p>{filtering && <button className="clear-filters" onClick={reset}><X size={13} /> Clear filters</button>}</div>
      {selected.length > 1 && <p className="filter-explanation">Showing models with all selected capabilities.</p>}
      <div aria-busy={query.trim().toLowerCase() !== deferredQuery}>
        {filtered.length === 0 ? <div className="directory-empty"><Search size={26} /><h2>No models match these filters</h2><p>Try a shorter name or remove a capability.</p><button className="btn btn-ink" onClick={reset}>Clear filters</button></div> : <div className="provider-list">{filtered.map(provider => <ProviderEntry key={`${provider.id}-${filtering}`} provider={provider} expanded={filtering} />)}</div>}
      </div>
    </div>
  );
}
