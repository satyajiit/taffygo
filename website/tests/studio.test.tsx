// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { act, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { WorkflowDemo } from "@/components/studio/WorkflowDemo";
import { ProviderDirectory } from "@/components/studio/ProviderDirectory";
import { EnginePresentation } from "@/components/studio/EnginePresentation";
import { initialSimulation, simulationReducer, type SimulationState } from "@/lib/simulation";
import directory from "@/lib/provider-directory.json";

afterEach(() => vi.useRealTimers());

it("stops the document demo at verification, requires a handback, and resets consent on replay", () => {
  vi.useFakeTimers();
  render(<WorkflowDemo />);
  fireEvent.click(screen.getByRole("button", { name: "Play demo" }));
  act(() => vi.advanceTimersByTime(5000));
  expect(screen.getByRole("heading", { name: "The website needs you" })).toBeInTheDocument();
  expect(screen.getByRole("button", { name: "Hand back to Taffy" })).toBeDisabled();
  act(() => vi.advanceTimersByTime(60_000));
  expect(screen.getByRole("button", { name: "Waiting for you" })).toBeDisabled();
  fireEvent.click(screen.getByRole("checkbox"));
  fireEvent.click(screen.getByRole("button", { name: "Hand back to Taffy" }));
  act(() => vi.advanceTimersByTime(5000));
  expect(screen.getByRole("link", { name: "Download sample PDF" })).toHaveAttribute("href", "/demo/residence-sample.pdf");
  fireEvent.click(screen.getByRole("button", { name: "Replay demo" }));
  act(() => vi.advanceTimersByTime(5000));
  expect(screen.getByRole("checkbox")).not.toBeChecked();
});

it("does not advance a banking task before its file is ready or its submission is approved", () => {
  let state: SimulationState = { ...initialSimulation, kind: "banking", playing: true, started: true };
  const tick = (ready = true) => { state = simulationReducer(state, { type: "tick", milliseconds: 10_000, fileReady: ready }); };
  tick(); tick(); tick();
  expect(state.step).toBe(3);
  tick(false);
  expect(state.step).toBe(3);
  tick(); tick();
  expect(state.step).toBe(5);
  state = simulationReducer(state, { type: "play" });
  tick();
  expect(state.step).toBe(5);
  expect(state.playing).toBe(false);
  state = simulationReducer(state, { type: "consent" });
  tick();
  expect(state.step).toBe(7);
  expect(state.playing).toBe(false);
  state = simulationReducer(state, { type: "switch", kind: "document" });
  expect(state).toEqual(initialSimulation);
});

it("pauses progression and ignores consent outside a handover", () => {
  const running = simulationReducer(initialSimulation, { type: "play" });
  const paused = simulationReducer(running, { type: "pause" });
  expect(simulationReducer(paused, { type: "tick", milliseconds: 50_000, fileReady: true })).toEqual(paused);
  expect(simulationReducer(running, { type: "consent" })).toEqual(running);
});

describe("model directory", () => {
  it("renders the full catalog before filtering by model", () => {
    const { container } = render(<ProviderDirectory />);
    expect(container.querySelectorAll(".provider-row")).toHaveLength(directory.providers.length);
    expect(container.querySelectorAll(".model-list li")).toHaveLength(directory.providers.reduce((sum, p) => sum + p.models.length, 0));
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "DeepSeek" } });
    for (const row of container.querySelectorAll(".provider-row")) {
      expect(row).toHaveAttribute("open");
      for (const model of row.querySelectorAll(".model-list li")) expect(`${row.querySelector(".provider-name")?.textContent} ${model.textContent}`.toLowerCase()).toContain("deepseek");
    }
  });

  it("combines capabilities and recovers from an empty result", () => {
    const { container } = render(<ProviderDirectory />);
    fireEvent.click(screen.getByRole("button", { name: "Images" }));
    fireEvent.click(screen.getByRole("button", { name: "Tools" }));
    expect(container.querySelectorAll(".model-list li").length).toBeGreaterThan(0);
    for (const item of container.querySelectorAll(".model-list li")) {
      expect(within(item as HTMLElement).getByText("Images")).toBeInTheDocument();
      expect(within(item as HTMLElement).getByText("Tools")).toBeInTheDocument();
    }
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "no-such-taffy-model" } });
    expect(screen.getByRole("heading", { name: "No models match these filters" })).toBeInTheDocument();
    fireEvent.click(screen.getAllByRole("button", { name: "Clear filters" })[0]!);
    expect(screen.getByRole("searchbox")).toHaveValue("");
    expect(screen.getByRole("status")).toHaveTextContent("667 model entries across 38 providers");
  });
});

it("exposes the browser and integrated Python worker separately", () => {
  render(<EnginePresentation />);
  fireEvent.click(screen.getByRole("button", { name: "Chromium" }));
  expect(screen.getByRole("heading", { name: "The browser that opens the web" })).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "On-device Python" }));
  expect(screen.getByRole("heading", { name: "Python is built into the app" })).toBeInTheDocument();
  expect(screen.getByText(/phone-level execution checks remain pending/)).toBeInTheDocument();
});
