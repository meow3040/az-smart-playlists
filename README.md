# Smart Playlists for XDJ-AZ

Tap **SMART PLAYLIST** on the normal browse/playlist screen to open live related tracks
for the track playing now. Matches follow its BPM and compatible musical keys. The
original three saved filter sets are still available on the mod's own page.

**Requirements:** an XDJ-AZ running AZ-MODS 1.2.3 or newer and an exported rekordbox USB
library. This is a custom extension of the upstream Library Browser example.

## Download and install

- [Download the latest test build](https://github.com/meow3040/az-smart-playlists/releases/tag/v1.2.0)
- [Installation and usage in Hebrew](library-browser/INSTALL-HE.md)
- [Filters, limits and build instructions](library-browser/README.md)

On the controller, open **Mod Menu > Community Mods > Repositories > Add** and enter:

```text
meow3040/az-smart-playlists
```

Then open **Browse**, select **Smart Playlists**, install it, and choose **Reload now**
when no set is playing. The public `azmods-repo.json` on the default branch points to the
release package and icon with its exact size and SHA-256. The repository works when added
directly; it does not need to be listed in the optional Mod Index.

The mod uses the independent id `meow-smart-playlists`, so it can coexist with the original
Library Browser. Version 1.1.0 used the original mod's id and was a manual-install test;
its saved filters do not automatically migrate to the new id.

For manual installation, the release also includes a ready-to-extract USB ZIP and the
equivalent `.azmod` package. Use one of them. The USB ZIP does not replace
`MODS/azmods.cfg`; follow the guide to enable the mod.

## Live SMART PLAYLIST

After installing or updating and choosing Reload now, open the controller's normal
playlist/browse screen. **SMART PLAYLIST** appears near the upper-right corner. Tap it to
open the overlay, and turn the browse knob to scroll. BACK or X closes it and gives the
controls back to the native browser. The SDK groups playlists, browsing and Tag List under
the same screen type, so the button is visible on those browse screens.

- **Reference:** Auto uses the sole playing deck. When several decks play, choose Deck 1,
  2, 3 or 4 explicitly. All four decks are inspected even in a two-deck view.
- **BPM:** tap to cycle tolerance through +/-2, 4, 6 or 10 BPM. The default is +/-4.
- **Key:** Compatible accepts the same Camelot key, its adjacent number on the same A/B
  wheel, or its relative major/minor. Any disables the key filter. When the reference key
  cannot be resolved, the list explicitly reports a BPM-only match.
- **Rescan USBs:** refresh the libraries after inserting or changing a USB. They also
  refresh on opening, reference-track changes and every 30 seconds while the overlay is open.

The reference is checked about once a second. Tempo changes refilter the cached library
without reading the USB again. Results come from both readable USBs, show their source,
exclude positively identified playing tracks, and sort by closest BPM first. Every match
within the scan is scrollable; live results do not have the saved list's 256-row display cap.
The scan is bounded at 20,000 tracks per USB and clearly reports incomplete scans.

**This is an overlay on the browse screen, not a stored native playlist.** It is view-only;
use the native browser to load a track. Live matching uses BPM and key, not genre or audio
analysis. The USB library is never modified.

## Saved filter sets

Open **Mod Menu > Community Mods > Installed > Smart Playlists**, then **Smart Playlists >**
under the selected USB. Choose Smart 1, 2 or 3 and edit its rules with the browse knob.
Settings save automatically. Select **Show matching tracks >** to rebuild the results.

| Filter | Choices |
|---|---|
| BPM | Minimum and maximum, inclusive; Any disables a bound |
| Genre | An exported playlist such as `Genre: House` or `Genre: Techno` |
| Key | Camelot 1A..12A / 1B..12B, with equivalent conventional short key names |
| Length | Under 3 minutes, 3 to under 6, 6 or more, or Any |
| Artist | Same artist as a loaded track on a chosen deck, or Any |
| Order | BPM ascending or descending, title, or artist |

All active filters must match. Rules persist and are shared across USB ports; results
come only from the selected port. Sorting happens before the first 256 matching tracks
are displayed. The core scans at most 20,000 tracks and reports an incomplete scan.

**These are view-only lists in the mod page.** They do not create playlists in the native
rekordbox browser and cannot load a track onto a deck. Use the native browser to load tracks.
The SDK does not expose genre tags: genre filtering uses explicit membership in uniquely
named `Genre: ...` playlists exported from rekordbox, with no inferred genre labels.

## Validation

The build is checked with focused rule, reader and UI tests, AddressSanitizer and UndefinedBehaviorSanitizer,
the SDK's functional and soak harness (including the live surface), a deliberately broken-copy control, and the SDK package
validator. The ARM64 binary was cross-compiled using GCC 7 and an Ubuntu 18.04 sysroot.
This is not the SDK's official reproducible-container build.

It has not been tested on a physical controller. The release is marked as a prerelease
for testing before use in a live set.

The build and tests used SDK commit `3dcaf8f7331ea94135d7161f653edfaf906d5ea6`.
For the focused Linux test runner, with that SDK checked out alongside this repository:

```sh
AZ_SDK=../az-mods-sdk sh tests/smart.sh
```

The Docker build and test fallback is documented in [the mod README](library-browser/README.md).
Source, focused tests, the SDK harness scenarios, and the local build container are included.

## Credits and license

Based on [Kyle-Hosman/az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples),
using the [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk). Original author credits
are retained. Smart playlist changes are maintained in this repository.

GPL-3.0-only; see [LICENSE](LICENSE). Not affiliated with AlphaTheta, Pioneer DJ or rekordbox.
