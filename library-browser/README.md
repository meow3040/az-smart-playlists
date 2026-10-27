# Library Browser

An example of the library services added in AZ-MODS 1.2.3: the rekordbox playlists on the
USBs in the deck, on a Mod Menu page. The knob moves, a click opens a playlist's tracks
(title, artist, BPM, key), BACK goes back to the list.

## Smart playlists

Open **Smart Playlists >** beneath a USB's existing playlists. Three saved rule sets start
with BPM ranges up to 109, 120 through 129, and 140 or higher. Select the Playlist row,
click, turn to Smart 1, 2 or 3, then click again. Each set is editable independently.

Turn to a setting, click to edit, turn to change it, and click or BACK to finish. Settings
save automatically. **Show matching tracks >** applies all the selected conditions together
to the current USB. BACK from the results returns to the rules; BACK again returns to the
USB playlists. Reset restores only the selected set's initial rules.

| Rule | Choices |
|---|---|
| BPM minimum / maximum | Inclusive whole-number bounds, 1..999; 0 means Any. Missing BPM does not match an active bound. |
| Genre | Membership in a playlist explicitly named `Genre: House`, `Genre: Techno`, etc., or Any. |
| Key | Any or Camelot 1A..12A / 1B..12B. Conventional short keys such as Am, C and F#m also match their Camelot equivalent. |
| Length | Any, under 3 minutes, 3 to under 6 minutes, or 6 minutes and longer. |
| Sort | BPM ascending, BPM descending, title, or artist. Unknown BPM sorts last. |
| Artist | Any, or the same artist as the loaded track on a selected deck. The artist is captured when Show is clicked; an unavailable deck reports an error. |

For example, select Smart 2, set minimum BPM to 122, maximum BPM to 128, Genre to House
and Key to 8A. The list contains only tracks in `Genre: House` that meet both the BPM and
key rules. To use this genre, first export that playlist and its tracks from rekordbox to
the USB. An intelligent playlist in rekordbox can maintain that membership before export.

The SDK does **not** expose a track's genre tag. This mod never guesses genre from tempo,
artist or title. Each genre name must identify exactly one playlist on that USB. Missing
or duplicate genre playlists report an error instead of showing unrelated tracks.

These are **view-only lists on the mod page**. They do not create native rekordbox
playlists and cannot load a track to a deck; use the native browser to load it. Filter
settings persist through the core settings service and are shared between USB ports.
Search results are rebuilt each time Show is clicked, rather than continuously while
the page is open. Genre names are resolved again on the current USB on every search.

The core scans at most 20,000 tracks per search; an incomplete scan is labelled
`[scan limited]`. Filtering and sorting happen before the first 256 matches are displayed.
The ordinary playlist tree displays at most 254 playlist/folder rows per port. Genre
choices come from that visible tree, while a saved genre is resolved against the core's
full playlist listing. This keeps the page and memory bounded. USB errors, empty results
and an incomplete scan have separate messages.

## What it shows

| Service | Where |
|---|---|
| `lib_playlists` | `read_list()`: every playlist and folder on the USB in the USB 1 port (USB 0) and the USB 2 port (USB 1), built into a tree |
| `lib_tracks` | `read_tracks()`: an ordinary playlist in its order, or every track (`playlist_id = 0`) filtered and sorted on the worker |
| `thread_start` | `init()`: the reader thread, where every library call is made |
| `cfg_get`, `cfg_set`, `cfg_get_str`, `cfg_set_str` | three independent saved rule sets (`smart-ui.h`) |
| `deck_info` | optional reference artist, captured when Show is clicked |
| `register_screen`, `draw_header`, `draw_text`, `fill_rect`, `text_width` | the page |

## Rules it follows

- The library services read the USB, so they are never called from `tick()` or a page
  callback (the core refuses them on the UI thread). The mod's own thread does the reading;
  the page only draws what that thread last read.
- The thread and the page share one mutex, held for a copy or a draw, never across a read.
- The callbacks copy what they need: the strings in `az_track` are valid during the callback
  only.
- The thread is started last in `init()`, after everything that can fail, because a thread
  outlives a refused `init()`.
- A port the core cannot tell (a USB behind a hub) has no library here: the call returns -1
  rather than guessing, and the page leaves that heading out.
- A return of 1 means the list was cut at the core's limit of 20,000 entries: the mod shows
  what it got rather than nothing.
- An entry whose parent is not in the listing is at the top level; the mod does not assume the
  top level's parent id.
- Reading is read-only: the core opens the library with a connection of its own that cannot
  write, so browsing never changes the USB.
- Every track request carries a generation. Results of a cancelled or superseded read
  cannot overwrite a newer request. Full playlist ids are retained without bit packing.

## Tests

`tests/smart.sh` runs rule, reader, settings and navigation checks, ASan/UBSan, the SDK's
functional and soak harness, and a broken-copy control that must fail. Set `AZ_SDK` to the
SDK checkout on a Linux host. The focused runner also exercises the smart page through
the SDK harness and `tests/scenarios.c`.

On hosts without ARM64 emulation, `tools/Dockerfile.smart-tests` provides a native test
runner and an ARM64 cross compiler. From the repository root:

```sh
docker build --platform linux/amd64 -f tools/Dockerfile.smart-tests -t az-smart-tests:local .
docker run --rm --platform linux/amd64 -v "$PWD":/repo:ro -v "$PWD/../az-mods-sdk":/sdk:ro az-smart-tests:local sh tests/smart.sh
docker run --rm --platform linux/amd64 -v "$PWD":/repo -v "$PWD/../az-mods-sdk":/sdk:ro az-smart-tests:local make -C library-browser CC=aarch64-linux-gnu-gcc AZ_SDK_INCLUDE=/sdk/include
python ../az-mods-sdk/tools/azmod-pack validate library-browser
python ../az-mods-sdk/tools/azmod-pack package library-browser
```

This fallback cross build is not the SDK's reproducible official-container build. Validate
its output and test on an emulator or a test deck before a live set. No physical deck is
exercised by the host tests.

## Build

From the repository root, with the SDK beside it:

```sh
../az-mods-sdk/tools/azmod-pack build library-browser
```

It needs the 1.2.3 SDK or newer.
