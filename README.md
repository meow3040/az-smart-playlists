# Smart Playlists for XDJ-AZ

Three saved smart lists inside the AZ-MODS Library Browser page. Combine a BPM range,
genre-playlist membership, musical key, track length and the artist on a selected deck.
Sort matches by BPM, title or artist. Each search reads the current USB library again.

**Requirements:** an XDJ-AZ running AZ-MODS 1.2.3 or newer and an exported rekordbox USB
library. This is a custom extension of the upstream Library Browser example.

## Download and install

- [Download the latest test build](https://github.com/meow3040/az-smart-playlists/releases/tag/v1.1.0)
- [Installation and usage in Hebrew](library-browser/INSTALL-HE.md)
- [Filters, limits and build instructions](library-browser/README.md)

The release includes a ready-to-extract USB ZIP and the equivalent `.azmod` package.
Use one of them. It updates the `library-browser` mod, so back up an existing installation
first. The USB ZIP does not replace `MODS/azmods.cfg`; follow the guide to enable the mod.

## What the lists do

Open **Mod Menu > Community Mods > Installed > Library Browser**, then **Smart Playlists >**
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

The initial test build passed 59 focused checks, AddressSanitizer and UndefinedBehaviorSanitizer,
the SDK's functional and soak harness, a deliberately broken-copy control, and the SDK package
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
