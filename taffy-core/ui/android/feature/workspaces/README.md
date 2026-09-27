# `:feature:workspaces`

**Status:** `[Current]` Where research lives: SCR-304, SCR-305, SCR-306, SCR-307
and SCR-309.

Owning milestone: M3 (the assistant and workspaces). Built here under work
package WP-M0-07.

Authoritative specifications:
screen-catalog.md rows SCR-304 to
SCR-307 and SCR-309;
ux-spec.md sections 6 and 7;
domain-model.md sections 13
and 14.

## The authority boundary

**A fact on these screens always knows where it came from.** Every cell carries
its kind and its sources, and the screen shows both. Three consequences are
enforced by the projections rather than by a reviewer:

- **a correction is recorded beside what the page said, never over it.** The
  correction sheet keeps both values on screen at once, and the sheet says how
  many other cells rest on the same source and would be recomputed;
- **excluding a source keeps the record.** The source stays visible and says it
  is excluded, and the cells that rested only on it are marked as needing a new
  source rather than quietly keeping a value nothing supports;
- **a saved workspace keeps the state it reached.** Partly done stays partly
  done, and stopped is never shown as finished.

The export sheet previews the file's own first lines, rendered by the same code
that would write it, so the sheet shows what will be written rather than
describing it.

## What this feature deliberately does not do

It runs no task and sends no task command: by the time a workspace
exists, the task that produced it has finished. It knows no other feature; the
assistant's task view has its own source and output panels, and the two do not
share components beyond `:core:ui`.

It writes no file itself. The export sheet renders the bytes and hands them to
the system document picker, which is the only path a file leaves by.

## What each screen owns

| Screen | Catalog row | Owns |
|---|---|---|
| `WorkspaceListScreen` | SCR-304 | Every workspace, newest first, in the state it reached; names the profile it belongs to when the device has more than one, and starts a new workspace through the template picker (decision 0102). On a tablet the list stays visible beside the open workspace |
| `WorkspaceDetailScreen` | SCR-305 | Output grouped by field, sources, conflicts, exclusions |
| `SourceViewerScreen` | SCR-306 | When and how one page was read |
| `FactCorrectionScreen` | SCR-307 | The user's value beside the page's, and what it affects |
| `ExportSheetScreen` | SCR-309 | The formats offered, and the file's own first lines |

## Verification list

Ordered, and each item is a gap rather than a silent hole:

1. **Two export formats, not four.** Spreadsheet and portable document formats
   belong to milestone M7. A format the UI host cannot produce byte-identically
   is absent rather than offered and disabled.
