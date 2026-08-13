
# -------- Options ----------

# Cloudsmith upload targets used by the CI build. Replace SET-CLOUDSMITH-ORG
# with a real organisation before enabling automated uploads. The values cannot
# be left empty — cmake/Metadata.cmake runs a regex over them and fails on an
# empty string.
set(OCPN_TEST_REPO
    "SET-CLOUDSMITH-ORG/deepreybathy-alpha"
    CACHE STRING "Default repository for untagged builds"
)
set(OCPN_BETA_REPO
    "SET-CLOUDSMITH-ORG/deepreybathy-beta"
    CACHE STRING "Default repository for tagged builds matching 'beta'"
)
set(OCPN_RELEASE_REPO
    "SET-CLOUDSMITH-ORG/deepreybathy-prod"
    CACHE STRING "Default repository for tagged builds not matching 'beta'"
)
option(DEEPREY_USE_SVG "Use SVG graphics" ON)

#
#
# -------  Plugin setup --------
#
# NOTE: PKG_NAME must stay "deepreybathy_pi" — GetPluginDataDir("deepreybathy_pi") is
# hardcoded in the sources and in the installed data path.
set(PKG_NAME deepreybathy_pi)
set(PKG_VERSION  1.0.0.0)
set(PKG_PRERELEASE "")  # Empty, or a tag like 'beta'

# Must be a single space-free token: PLUGIN_API_NAME is concatenated into the
# tarball and metadata filenames, and it has to match GetCommonName() in the
# plugin so the catalog entry lines up. Catalog convention is one word.
set(DISPLAY_NAME "Deeprey Bathymetry")  # Dialogs, installer artifacts, ...
set(PLUGIN_API_NAME "DeepreyBathymetry") # As of GetCommonName() in plugin API
set(PKG_SUMMARY "Record depth soundings and build your own bathymetric chart")
set(PKG_DESCRIPTION [=[
Record depth soundings from your echosounder as you sail, and build your own
high-resolution bathymetric chart, rendered live on the chart canvas.

Depth is read from standard NMEA 0183 DPT and DBT sentences and tagged with your
GPS position. Soundings accumulate in a local tile database and are drawn as a
colour-shaded depth surface with automatic contour lines. Existing survey data
can be imported from, and exported to, XYZ files.
]=])

set(PKG_AUTHOR "Deeprey Research Ltd")
set(PKG_IS_OPEN_SOURCE "yes")
<<<<<<< HEAD
# PKG_HOMEPAGE fills <source> in the catalog metadata: the link users follow to
# the source. PKG_INFO_URL fills <info-url>, the plugin's home page.
set(PKG_HOMEPAGE https://github.com/deeprey-marine/deepreybathy-pi)
=======
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/deeprey-marine/deepreybathy-pi)
>>>>>>> 3b8d03b (Deeprey Bathymetry: bathymetry recording plugin for OpenCPN)
set(PKG_INFO_URL https://github.com/deeprey-marine/deepreybathy-pi)

# ----------------------------------------------------------------------------------------------------------------#

# ----------------------------------------------------------------------------------------------------------------#

#UI
set(UI_SOURCES
    src/ui/DeepreyBathyPrefsDialog.cpp
)

set(UI_HEADERS
    include/ui/DeepreyBathyPrefsDialog.h
)

#core

set(CORE_SOURCES
    src/core/BathymetryDataManager.cpp
    src/core/BathymetryRenderer.cpp
    src/core/BathymetryRenderParams.cpp
    src/core/FloatGrid.cpp
    src/core/ImportExportManager.cpp
    src/core/PbgComputeShader.cpp
    src/core/PbgDilationRenderer.cpp
    src/core/PbgDilationShader.cpp
    src/core/PbgImportFilter.cpp
    src/core/PbgPointRenderer.cpp
    src/core/PbgPointShader.cpp
    src/core/PbgPyramidBuilder.cpp
    src/core/PbgRenderCacheManager.cpp
    src/core/PbgSeedsReaderWriter.cpp
    src/core/PbgTexture.cpp
    src/core/PbgTileDatabase.cpp
    src/core/PbgUnifiedShader.cpp
    src/core/RealtimePointInjector.cpp
    src/core/ShaderUtils.cpp
    src/core/stb_impl.cpp
    src/core/RealTimePointFilter.cpp
    src/core/DirtyTileTracker.cpp
    src/core/AsyncPersistenceManager.cpp
)

set(CORE_HEADERS
    include/core/BathymetryDataManager.h
    include/core/BathymetryRenderer.h
    include/core/BathymetryRenderParams.h
    include/core/DpPbg.h
    include/core/FloatGrid.h
    include/core/ImportExportManager.h
    include/core/IsobathParameters.h
    include/core/MercatorSize.h
    include/core/MinMax.h
    include/core/PbgComputeShader.h
    include/core/PbgDilationRenderer.h
    include/core/PbgDilationShader.h
    include/core/PbgImportFilter.h
    include/core/PbgPointRenderer.h
    include/core/PbgPointShader.h
    include/core/PbgPyramidBuilder.h
    include/core/PbgRenderCacheManager.h
    include/core/PbgSeedsReaderWriter.h
    include/core/PbgTexture.h
    include/core/PbgTileDatabase.h
    include/core/PbgUnifiedShader.h
    include/core/Position.h
    include/core/PlugIn_ViewPort.h
    include/core/Rcl.h
    include/core/RclExtensions.h
    include/core/RclWorld.h
    include/core/RealtimePointInjector.h
    include/core/ShaderUtils.h
    include/core/stb_image.h
    include/core/TileInfo.h
    include/core/RealTimePointFilter.h
    include/core/DirtyTileTracker.h
    include/core/AsyncPersistenceManager.h
)

set(GLEW_SOURCES
    libs/glew/src/glew.c
)

# Ui
source_group("UI\\Source" FILES ${UI_SOURCES})
source_group("UI\\Headers" FILES ${UI_HEADERS})

#core
source_group("CORE\\Source" FILES ${CORE_SOURCES})
source_group("CORE\\Headers" FILES ${CORE_HEADERS})

set(SRC
    src/DeepreyBathy_pi.cpp
    # separated controls
    ${UI_SOURCES}
    ${CORE_SOURCES}
    ${GLEW_SOURCES}
)

set(HEADERS
    include/DeepreyBathy_pi.h
    # separated headers
    ${UI_HEADERS}
    ${CORE_HEADERS}
)

# Force files to appear at the root level instead of "Header Files" or "Source Files"
source_group("" FILES
    ${SRC}
    ${HEADERS}
)

add_definitions("-DocpnUSE_GL")

# API 1.18 covers OpenCPN 5.8.0 through 5.14.0 — by far the widest install base,
# and what the live plugin catalog overwhelmingly targets. Everything this plugin
# needs (GetListener/NMEA0183Id, SetPositionFixEx, ShowPreferencesDialog,
# InsertPlugInToolSVG, RenderGLOverlayMultiCanvas with priority) exists at 1.18.
set(PKG_API_LIB api-18)  #  A directory in opencpn-libs/ e. g., api-18 or api-21

macro(late_init)
  # Perform initialization after the PACKAGE_NAME library, compilers
  # and ocpn::api is available.
  if (DEEPREY_USE_SVG)
    target_compile_definitions(${PACKAGE_NAME} PUBLIC DEEPREY_USE_SVG)
  endif ()
endmacro ()

macro(add_plugin_libraries)
  # Add libraries required by this plugin
  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/plugin_dc")
  target_link_libraries(${PACKAGE_NAME} ocpn::plugin-dc)

  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/wxJSON")
  target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

  if (CMAKE_HOST_WIN32)
    target_include_directories(${PACKAGE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/libs/glew/include)
    target_compile_definitions(${PACKAGE_NAME} PRIVATE GLEW_STATIC)
  elseif (NOT APPLE)
    find_package(GLEW REQUIRED)
    target_include_directories(${PACKAGE_NAME} PRIVATE ${GLEW_INCLUDE_DIRS})
    target_link_libraries(${PACKAGE_NAME} ${GLEW_LIBRARIES})
  endif ()
  
  set(SQLITE_DIR "${CMAKE_SOURCE_DIR}/libs/wxsqlite3")
  
  add_library(sqlite_internal STATIC
    ${SQLITE_DIR}/sqlite3mc_amalgamation.c
    ${SQLITE_DIR}/wxsqlite3.cpp
  )
  
  target_include_directories(sqlite_internal PRIVATE
    ${SQLITE_DIR}
    ${SQLITE_DIR}/wx
  )
  
  target_compile_definitions(sqlite_internal PRIVATE
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_RTREE
    SQLITE_ENABLE_JSON1
  )
  
  if(MSVC)
    target_compile_options(sqlite_internal PRIVATE /wd4996 /wd4267 /wd4244)
  else()
    # Enable AES-NI intrinsics for SQLite encryption
    target_compile_options(sqlite_internal PRIVATE -maes -msse4.2)
  endif()
  
  target_include_directories(${PACKAGE_NAME} PRIVATE
    ${SQLITE_DIR}
    ${SQLITE_DIR}/wx
  )

  # NMEA 0183 sentence parser (DPT / DBT) from opencpn-libs
  add_subdirectory("${CMAKE_SOURCE_DIR}/opencpn-libs/nmea0183")
  target_link_libraries(${PACKAGE_NAME} ocpn::nmea0183)

  # Lier SQLite au plugin
  target_link_libraries(${PACKAGE_NAME} sqlite_internal)
  
  find_package(OpenGL REQUIRED)
  target_link_libraries(${PACKAGE_NAME} ${OPENGL_LIBRARIES})

endmacro ()
