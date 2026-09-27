// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import { fireEvent, render, screen } from "@testing-library/react";
import { expect, it } from "vitest";
import { NewTabPreview } from "@/components/studio/NewTabPreview";
import { BrowserWorkbench } from "@/components/studio/BrowserWorkbench";
import { PageAssistantPreview } from "@/components/studio/PageAssistantPreview";
import { LocalToolsPreview } from "@/components/studio/LocalToolsPreview";

it("changes from the idle new tab to intent choices and restores the idle UI when cleared", () => {
  render(<NewTabPreview />);
  expect(screen.getByText("Where to today?")).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Show evening scene" }));
  expect(screen.getByAltText(/evening scene/)).toHaveAttribute("src", "/start-scenes/evening-320.webp");
  fireEvent.change(screen.getByRole("textbox"), { target: { value: "Plan a weekend" } });
  expect(screen.queryByText("Where to today?")).not.toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: /Task for Taffy/ }));
  expect(screen.getByRole("status")).toHaveTextContent("This is a preview");
  fireEvent.click(screen.getByRole("button", { name: "Clear the address box" }));
  expect(screen.getByText("Where to today?")).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Preview downloads" }));
  expect(screen.getByRole("link", { name: "Open sample PDF" })).toHaveAttribute("download");
});

it("lets visitors try private tabs, select Library details, and change sample site blocking", () => {
  render(<BrowserWorkbench />);
  fireEvent.click(screen.getByRole("button", { name: "Private" }));
  fireEvent.click(screen.getByRole("button", { name: "Open sample tab" }));
  expect(screen.getByRole("heading", { name: "New private tab" })).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Library" }));
  fireEvent.click(screen.getByRole("button", { name: /Application photo/ }));
  expect(screen.getByRole("status")).toHaveTextContent("Application photo selected");
  fireEvent.click(screen.getByRole("button", { name: "Ad blocking" }));
  const toggle = screen.getByRole("switch");
  expect(toggle).toHaveAttribute("aria-checked", "true");
  fireEvent.click(toggle);
  expect(toggle).toHaveAttribute("aria-checked", "false");
  expect(screen.getByText("blocking paused for this example")).toBeInTheDocument();
});

it("keeps the answer and its source link matched to the selected question", () => {
  render(<PageAssistantPreview />);
  fireEvent.click(screen.getByRole("button", { name: "What do I need?" }));
  expect(screen.getByRole("status")).toHaveTextContent("Carrots, red lentils");
  expect(screen.getByRole("link", { name: "Ingredients" })).toHaveAttribute("href", "#recipe-ingredients");
  fireEvent.click(screen.getByRole("button", { name: "What’s the first step?" }));
  expect(screen.getByRole("link", { name: "Method, step 1" })).toHaveAttribute("href", "#recipe-method");
});

it("switches between document and spreadsheet sample outputs", () => {
  render(<LocalToolsPreview />);
  expect(screen.getByText("weekend-brief.docx")).toBeInTheDocument();
  fireEvent.click(screen.getByRole("button", { name: "Spreadsheet" }));
  expect(screen.getByRole("table", { name: "Sample trip expense data" })).toBeInTheDocument();
  expect(screen.queryByText("weekend-brief.docx")).not.toBeInTheDocument();
});
