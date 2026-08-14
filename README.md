# Deeprey Bathymetry

**Record depth soundings from your echosounder as you sail, and build your own
high-resolution bathymetric chart, rendered live on OpenCPN.**

Deeprey Bathymetry listens to the depth sentences your instruments already broadcast,
tags each reading with your GPS position, and accumulates them into a local
tile database. The result is drawn straight onto the chart as a colour-shaded
depth surface with automatic contour lines — your own survey of the water you
actually sail.

---

## What it does

- **Records depth automatically.** Reads standard NMEA 0183 `DPT` and `DBT`
  sentences. Nothing to configure beyond ticking a box.
- **Corrects for your transducer.** When your sounder sends `DPT`, its
  transducer-offset field is applied, so recorded depths are comparable
  between boats rather than "however deep my sensor happens to be mounted".
- **Draws as you go.** Soundings appear on the chart immediately, interpolated
  into a continuous surface with automatic contour lines.
- **Filters bad data.** Out-of-range depths, readings taken at speed, sudden
  spikes and a stuck transducer repeating one value are all rejected before
  anything is stored.
- **Imports and exports XYZ.** Bring in existing survey data, or take yours
  elsewhere.

## Requirements

- OpenCPN 5.12.0 or newer (plugin API 1.21 — required for tide correction)
- A depth sounder that outputs NMEA 0183 `DPT` or `DBT`
- A GPS position source
- OpenCPN's tide/current data (tcdata) configured, for reduction to chart datum

OpenGL is used for rendering; any machine that runs OpenCPN's chart canvas
will run Deeprey Bathymetry.

## Using it

1. Enable **Deeprey Bathymetry** in Options → Plugins.
2. Open its **Preferences** and tick *Record depth soundings from NMEA*.
3. Go sailing.

The toolbar button toggles the chart overlay on and off. Everything else lives
in a single preferences window:

| Setting | What it does |
|---|---|
| Show bathymetry overlay | Draws the recorded depth surface on the chart |
| Show contour lines | Adds automatic depth contours |
| Opacity | How much of the underlying chart shows through |
| Record depth soundings from NMEA | Starts and stops recording |
| Filter out bad depth readings | Applies the data quality filters |

Recorded data is stored in a SQLite database alongside your OpenCPN
configuration. The Preferences window shows its exact location, size and point
count, and offers Import, Export and Clear.

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . --target tarball
```

Requires wxWidgets 3.2+, OpenGL and a C++17 compiler. The `opencpn-libs`
submodule supplies the plugin API and the NMEA 0183 parser:

```bash
git submodule update --init --recursive
```

## Licence

GPL-2.0-or-later — see [LICENSE](LICENSE). Third-party components and their
licences are listed in [THIRD-PARTY.md](THIRD-PARTY.md).

## Credits

Developed by [Deeprey Research Ltd](https://deeprey.com).

Deeprey Bathymetry is the 2D bathymetry engine from Deeprey's marine display software,
released for the OpenCPN community.
