// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

export type DemoKind = "document" | "banking";
export type DemoStep = { title: string; detail: string; duration: number; gate?: "verification" | "approval" };

export const demoSteps: Record<DemoKind, DemoStep[]> = {
  document: [
    { title: "Find the official service", detail: "Searching for the document service and checking the destination.", duration: 2400 },
    { title: "Open your records", detail: "Following the document link on the service’s website.", duration: 2400 },
    { title: "Complete verification", detail: "A CAPTCHA or sign-in needs you. Taffy stops here.", duration: 0, gate: "verification" },
    { title: "Select the document", detail: "You handed back. Taffy opens the requested record.", duration: 2400 },
    { title: "Download the PDF", detail: "Saving the file from the document page.", duration: 2200 },
    { title: "Document saved", detail: "The file is ready in Downloads.", duration: 0 },
  ],
  banking: [
    { title: "Open the application", detail: "Reading the bank’s form and its attachment requirements.", duration: 2000 },
    { title: "Read your Library", detail: "Using the details and photo you selected for this task.", duration: 2200 },
    { title: "Fill the form", detail: "Copying your selected details into the matching fields.", duration: 3800 },
    { title: "Resize the photo", detail: "The form needs a photo under 200 KB. Preparing a smaller copy.", duration: 2800 },
    { title: "Attach the photo", detail: "Adding the resized copy to the application.", duration: 2000 },
    { title: "Review the application", detail: "Check the details and attachment before approving submission.", duration: 0, gate: "approval" },
    { title: "Submit the form", detail: "You approved. Sending the completed application.", duration: 2000 },
    { title: "Application received", detail: "The confirmation is ready to save.", duration: 0 },
  ],
};

export type SimulationState = { kind: DemoKind; step: number; elapsed: number; playing: boolean; started: boolean };
export type SimulationEvent =
  | { type: "tick"; milliseconds: number; fileReady: boolean }
  | { type: "play" | "pause" | "consent" | "restart" }
  | { type: "switch"; kind: DemoKind };

export const initialSimulation: SimulationState = { kind: "document", step: 0, elapsed: 0, playing: false, started: false };

function nextStep(state: SimulationState): SimulationState {
  const step = Math.min(state.step + 1, demoSteps[state.kind].length - 1);
  const next = demoSteps[state.kind][step]!;
  return { ...state, step, elapsed: 0, playing: next.duration > 0 };
}

/** Only a consent event can cross a verification or submission boundary. */
export function simulationReducer(state: SimulationState, event: SimulationEvent): SimulationState {
  const current = demoSteps[state.kind][state.step]!;
  switch (event.type) {
    case "switch": return { ...initialSimulation, kind: event.kind };
    case "restart": return { ...initialSimulation, kind: state.kind, playing: true, started: true };
    case "pause": return { ...state, playing: false };
    case "play": return { ...state, playing: current.duration > 0, started: true };
    case "consent": return current.gate ? nextStep(state) : state;
    case "tick": {
      if (!state.playing || current.gate || current.duration === 0) return state;
      const elapsed = Math.min(state.elapsed + event.milliseconds, current.duration);
      if (elapsed < current.duration) return { ...state, elapsed };
      if (state.kind === "banking" && state.step === 3 && !event.fileReady) return { ...state, elapsed };
      return nextStep(state);
    }
  }
}

export function typedValue(value: string, elapsed: number, offset = 0): string {
  return value.slice(0, Math.max(0, Math.floor((elapsed - offset) / 55)));
}
