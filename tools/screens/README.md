# tools/screens

Screenshots of the real app, taken from an Android phone over adb, for the
store listing and the website.

This directory owns the capture step and nothing after it. Captures are
written to a run directory outside this repository, not committed.
Framing, Play listing images and website images are made from them
somewhere else.

## Use

```bash
PHONE=192.0.2.20:5555      # a Wi-Fi serial from `adb devices`, or a USB one
./tools/screens/capture --insets --device "$PHONE"
./tools/screens/capture --device "$PHONE" --out ~/screens/2026-09-26 \
    --shows "The start page" 01-start-page
./tools/screens/capture --demo-off --device "$PHONE"
```

Put the phone on the screen you want, then run `capture` with a name. It
writes three files and one manifest entry:

- `raw/<name>.png`: the screencap exactly as the phone sent it.
- `cropped/<name>.png`: the same pixels, full resolution, without the system
  bars.
- `webp/<name>.webp`: the crop as WebP, quality 90 unless `--quality` says
  otherwise.
- `manifest.json`: for each capture, what it shows, its pixel sizes, each crop
  in pixels with its reason, and the app's versionName and versionCode.
  `--substitute` records a capture that stands in for a screen the phone could
  not show.

Running `capture` again with the same name replaces that capture and its
manifest entry.

## What it does to the phone

For one capture, it turns SystemUI demo mode on and asks for 12:00, a full
battery and no notification icons. Then it captures the screen and turns demo
mode off again. It puts `sysui_demo_allowed` back as it found it, even when
the run fails. If the adb connection drops before that can happen, it says so;
`--demo-off` puts the phone back.

It installs nothing, opens nothing and taps nothing. Getting the phone onto
the right screen is your job.

## The crops

The crops come from the phone. The navigation bar is always cut, at the top
edge of the `navigationBars` frame that `dumpsys window` reports. The status
bar is kept only when demo mode worked. The tool reads the status-bar clock
back through `uiautomator dump --windows`, and anything other than 12:00 cuts
the bar at the `statusBars` frame. HyperOS on Android 16 hides the
notification icons but keeps its own clock, so on the phone this was written
against the status bar is always cut: 156 px off the top and 52 px off the
bottom of a 1268x2756 screen.

If a notification is drawn over the app, the tool waits until it has gone.
A frame taken while a notification was showing is thrown away and taken again.

## Requirements

`adb`, and a `python3` that can import Pillow. This repository installs
nothing at run time, so if Pillow is missing, create a venv with it and set
`TAFFY_SCREENS_PYTHON` to that venv's `python3`.

Nothing in `./tools/check` runs this command, because it needs a phone.
`frame.py` holds the image and manifest half. It never talks to the phone.
