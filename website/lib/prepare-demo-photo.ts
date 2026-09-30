// Copyright (c) 2026 Matterward Labs Private Limited.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

export type PreparedPhoto = { blob: Blob; originalBytes: number; width: number; height: number };

/** Resizes a local sample. No visitor files, accounts, or external requests. */
export async function prepareDemoPhoto(source: string, signal: AbortSignal): Promise<PreparedPhoto> {
  const response = await fetch(source, { signal });
  if (!response.ok) throw new Error("The sample photo could not be loaded.");
  const original = await response.blob();
  const bitmap = await createImageBitmap(original);
  try {
    const canvas = document.createElement("canvas");
    canvas.width = 480;
    canvas.height = Math.round(bitmap.height * (480 / bitmap.width));
    const context = canvas.getContext("2d");
    if (!context) throw new Error("Image preparation is unavailable in this browser.");
    context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
    const blob = await new Promise<Blob>((resolve, reject) => canvas.toBlob(
      result => result ? resolve(result) : reject(new Error("The photo could not be resized.")), "image/jpeg", 0.84,
    ));
    signal.throwIfAborted();
    if (blob.size >= 200_000) throw new Error("The prepared photo is still larger than 200 KB.");
    return { blob, originalBytes: original.size, width: canvas.width, height: canvas.height };
  } finally {
    bitmap.close();
  }
}
