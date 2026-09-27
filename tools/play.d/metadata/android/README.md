# Generated store-listing metadata

Every file here is projected from `tools/play.d/store.toml` by
`tools/lib/play_listing.py`, in the Fastlane-shaped layout `gplay sync`
reads. Do not edit them: `./tools/check fast --only play` compares the
two in both directions, so a hand edit here fails the gate rather than
being quietly overwritten by the next publish.

Change the listing in `store.toml`, then run
`python3 tools/lib/play_listing.py --write`.

The listing IMAGES live beside these files, under each locale's
`images/` directory, because that is the Fastlane layout
`gplay images sync` reads. They are produced by a different tool from a
different source, so `--write` deletes only the text files it owns and
never the tree.
