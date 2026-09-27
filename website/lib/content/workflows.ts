// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

export const workflows = [
  {
    id: "document", label: "A government document", request: "Download my government document.",
    description: "Find the service, complete verification, and save the document.",
    file: "Your document.pdf", kind: "PDF document",
    steps: [
      { title: "Find the official website", body: "Taffy finds the relevant government service and shows you the destination before continuing.", actor: "Taffy browses", detail: "Official service located", icon: "globe" },
      { title: "Navigate to your document", body: "Taffy follows the service’s pages to the document request. You can watch, pause, or take over.", actor: "Taffy browses", detail: "Opening the document service", icon: "route" },
      { title: "Hand over the sensitive step", body: "A CAPTCHA, sign-in, one-time code, or sensitive field means it is your turn. You complete it directly on the website.", actor: "Waiting for you", detail: "Complete this step yourself", icon: "hand" },
      { title: "Hand back to Taffy", body: "When you are ready, hand the page back. Taffy continues from the page you left open.", actor: "Taffy browses", detail: "Continuing with your permission", icon: "route" },
      { title: "Keep the document", body: "Taffy reaches the download and saves the document to your phone. You check the resulting file.", actor: "Done", detail: "Ready to open on your phone", icon: "file" },
    ],
  },
  {
    id: "banking", label: "A banking form", request: "Help me fill my banking form.",
    description: "Fill selected details, prepare the photo, and review the form.",
    file: "Application photo.jpg", kind: "Example: 184 KB · under 200 KB",
    steps: [
      { title: "Open the bank’s form", body: "You choose the bank and form. Taffy opens the expected website and checks the fields it asks for.", actor: "Taffy browses", detail: "Your chosen bank’s application", icon: "globe" },
      { title: "Use the details you choose", body: "Taffy uses the saved details and files you select from your Library. You review what will go into the form.", actor: "Browse together", detail: "Saved details, selected by you", icon: "library" },
      { title: "Make the image fit", body: "The form asks for an image below 200 KB. Taffy resizes or compresses a copy to meet the requirement and keeps your original.", actor: "Taffy browses", detail: "Example: 1.8 MB → 184 KB", icon: "image" },
      { title: "Attach the prepared image", body: "Taffy adds the prepared copy to the upload field. You handle sign-in, CAPTCHA, and sensitive information yourself.", actor: "Browse together", detail: "Image prepared for the upload field", icon: "file" },
      { title: "Review, then approve submission", body: "Check the completed form and attachment. Taffy asks for your approval before submitting; a banking task never authorizes a payment.", actor: "Waiting for you", detail: "Review the form before it is sent", icon: "hand" },
      { title: "Check the confirmation", body: "After your approval, the intended flow submits the form and shows the bank’s confirmation. If the site refuses, the task must say so.", actor: "Done", detail: "Check the bank’s response", icon: "check" },
    ],
  },
] as const;

export const workflowNotice = "Interactive demos with sample data. Government-document downloads and this complete banking flow have not yet been verified end to end on a phone. Site rules and provider capabilities can change the steps.";
