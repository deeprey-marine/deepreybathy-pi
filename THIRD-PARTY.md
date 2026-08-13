# Third-party components

Deeprey Bathymetry is licensed GPL-2.0-or-later; see [LICENSE](LICENSE). The components
below are covered by their own licences, all compatible with GPLv2-or-later.

## Vendored in this repository

| Path | Component | Licence |
|---|---|---|
| `libs/glew/` | GLEW — OpenGL Extension Wrangler | Modified BSD, Mesa 3-D, Khronos |
| `libs/wxsqlite3/` | wxSQLite3 / SQLite3 Multiple Ciphers | wxWindows Library Licence |
| `include/core/stb_image.h` | stb_image v2.30 | Public domain / MIT |

Only the subset the build consumes is vendored; see `libs/glew/README.md`.

## Fetched as a submodule

`opencpn-libs/` (https://github.com/OpenCPN/opencpn-libs) is GPL-2.0-or-later
and is not redistributed in this repository. The following parts are compiled
into the built plugin binary:

| Component | Copyright |
|---|---|
| Plugin API (`api-18/ocpn_plugin.h`) | David S. Register |
| NMEA 0183 parser (`nmea0183/`) | David S. Register and contributors |
| Plugin device context (`plugin_dc/`) | Jon Gough |
| wxJSON (`wxJSON/`) | Luciano Cattani |

## Build tooling

`cmake/` and `ci/` come from the OpenCPN plugin template and carry their own
licences — mostly GPL-3.0-or-later, with `cmake/PatchFile.cmake`,
`ci/download-wx32.sh`, `ci/travis-build-win32.sh` and `ci/upload.*.in` under MIT.
These scripts build and package the plugin; they are not compiled into it, and
they remain under their own terms. Their presence alongside the GPLv2+ plugin
is mere aggregation and does not relicense either work.

## Build dependencies

wxWidgets (wxWindows Library Licence) and OpenGL are required to build but are
not distributed here. On Windows, `buildwin/win_deps.bat` fetches the wxWidgets
headers into `cache/`.
