# ============================================================================
# PlatformEditor -- EmulatR manifest authoring/validation tool
# ============================================================================
# Qt-free DOM + ordered/format-preserving JSON I/O core (Section 5.1) with a
# TUI and the D-025 Qt Widgets frontend.  Builds standalone (cmake this dir)
# or as an EmulatR sub-project (root option EMULATR_BUILD_EDITORS, default ON);
# either way the runtime targets land in <build>/editors/.
#
# All test targets are namespaced platedit_* so inclusion in the EmulatR
# superbuild cannot collide with emulator target names.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(PlatformEditor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Runtime output: <config>/editors -- the editor binaries land BESIDE
# Emulatr[.exe], in one self-contained editors/ subdir of the RUN dir
# (2026-07-25, user direction).  $<TARGET_FILE_DIR:Emulatr> is the only
# expression that lands correctly on both host layouts:
#   MSVC in-source VS build : <root>/<Config>/editors  -- build_emulatr.sh
#                             mirrors <root>/<Config>/ into
#                             out/build/<config>/, so editors/ rides along
#                             with no change to the script
#   Ninja out-of-source     : out/build/<config>/editors
# The value deliberately CONTAINS a generator expression, which is what stops
# multi-config generators from appending /<Config>.  The previous plain
# "${CMAKE_BINARY_DIR}/editors" resolved to <root>/editors/<Config> on the
# in-source VS build: 44 MB of exes + Qt DLLs dumped into the SOURCE tree, and
# outside the mirror, so the run dir never got the editor at all.
# Standalone (no Emulatr target) keeps the plain per-build path.
if(TARGET Emulatr)
  set(PLATEDIT_RUNTIME_DIR "$<TARGET_FILE_DIR:Emulatr>/editors")
else()
  set(PLATEDIT_RUNTIME_DIR "${CMAKE_BINARY_DIR}/editors")
endif()

# --- Qt-free core library ---------------------------------------------------
add_library(platedit_core STATIC
  core/OrderedJson.cpp
  core/Path.cpp
  core/SchemaPolicy.cpp
  core/DeviceCatalog.cpp
  core/ManifestView.cpp
  core/Validate.cpp
  tui/Renderer.cpp
)
target_include_directories(platedit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Test executables (namespaced; see header) -------------------------------
add_executable(platedit_roundtrip_test tests/roundtrip_test.cpp)
add_executable(platedit_schema_test    tests/schema_test.cpp)
add_executable(platedit_catalog_test   tests/catalog_test.cpp)
add_executable(platedit_view_test      tests/view_test.cpp)
add_executable(platedit_structural_test tests/structural_test.cpp)
foreach(t platedit_roundtrip_test platedit_schema_test platedit_catalog_test
          platedit_view_test platedit_structural_test)
  target_link_libraries(${t} PRIVATE platedit_core)
endforeach()

# --- TUI application --------------------------------------------------------
add_executable(platedit_tui tui/main.cpp)
target_link_libraries(platedit_tui PRIVATE platedit_core)
set_target_properties(platedit_tui PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PLATEDIT_RUNTIME_DIR}")

# --- Qt Widgets frontend (D-025 shipping frontend) ---------------------------
# Gated: absent Qt, the core/TUI/tests still configure and build everywhere.
# AUTOMOC is enabled per-target; no Qt types below qt/ (D-025/D-026).
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
  add_executable(platedit_qt
    qt/main.cpp
    qt/MainWindow.cpp
    qt/ManifestModel.cpp
    qt/PropertyPane.cpp
    qt/MainWindow.h
    qt/ManifestModel.h
    qt/PropertyPane.h
    qt/EditOps.h
  )
  set_target_properties(platedit_qt PROPERTIES
    AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY "${PLATEDIT_RUNTIME_DIR}")
  if(APPLE)
    # Bundle so macOS gives the app a reliable global menu bar + dock entry.
    set_target_properties(platedit_qt PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "EmulatR Platform Editor"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.envysys.emulatr.platformeditor")
  endif()
  target_link_libraries(platedit_qt PRIVATE platedit_core Qt6::Widgets)
  target_compile_definitions(platedit_qt PRIVATE
    PLATEDIT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  # Stage the schema + catalog BESIDE THE EXECUTABLE so a deployed editors/
  # dir is self-contained (main.cpp resolution order: env, exe dir, source
  # dir).  $<TARGET_FILE_DIR> tracks the generator: VS multi-config appends
  # <Config>/, the macOS bundle resolves to Contents/MacOS/ -- both are
  # exactly where applicationDirPath() looks at run time.
  add_custom_command(TARGET platedit_qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/schema"
            "$<TARGET_FILE_DIR:platedit_qt>/schema"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
            "$<TARGET_FILE_DIR:platedit_qt>/catalog"
    COMMENT "[PlatformEditor] stage schema/ + catalog/ beside platedit_qt"
    VERBATIM)

  # --- Windows: Qt runtime DLLs beside the editor exe -----------------------
  # platedit_qt lands in <config>/editors/ and needs Qt6Widgets/Gui/Core next to
  # it to launch standalone -- a second copy of the Qt runtime one level below
  # Emulatr.exe's, which is deliberate: editors/ stays independently shippable.
  # This POST_BUILD must live HERE, not in the
  # EmulatR root CMakeLists: add_custom_command(TARGET ...) is only legal in the
  # directory that created the target.
  #
  # Tool resolution order: an operator override (-DWINDEPLOYQT_EXECUTABLE=<path>,
  # also honoured by the root project), then the bin/ dir of the Qt6 package
  # find_package() just resolved above -- so the deployed DLLs always match the
  # Qt this target actually linked against.
  if(WIN32)
    set(_platedit_qt_bin_hints "")
    if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_BINS)
      list(APPEND _platedit_qt_bin_hints "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    endif()
    if(TARGET Qt6::qmake)
      get_target_property(_platedit_qmake Qt6::qmake IMPORTED_LOCATION)
      if(_platedit_qmake)
        get_filename_component(_platedit_qmake_dir "${_platedit_qmake}" DIRECTORY)
        list(APPEND _platedit_qt_bin_hints "${_platedit_qmake_dir}")
      endif()
    endif()

    if(DEFINED WINDEPLOYQT_EXECUTABLE AND EXISTS "${WINDEPLOYQT_EXECUTABLE}")
      set(PLATEDIT_WINDEPLOYQT "${WINDEPLOYQT_EXECUTABLE}")
    else()
      find_program(PLATEDIT_WINDEPLOYQT
        NAMES windeployqt windeployqt.exe
        HINTS ${_platedit_qt_bin_hints}
        DOC "windeployqt used to stage Qt DLLs beside platedit_qt")
    endif()

    if(PLATEDIT_WINDEPLOYQT AND EXISTS "${PLATEDIT_WINDEPLOYQT}")
      add_custom_command(TARGET platedit_qt POST_BUILD
        COMMAND "${PLATEDIT_WINDEPLOYQT}"
                --verbose 1
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                "$<TARGET_FILE:platedit_qt>"
        COMMENT "[PlatformEditor] windeployqt: copying Qt runtime DLLs"
        VERBATIM)
      message(STATUS "PlatformEditor: Qt deployment configured: ${PLATEDIT_WINDEPLOYQT}")
    else()
      message(WARNING
        "PlatformEditor: windeployqt not found -- platedit_qt.exe will not launch "
        "without Qt DLLs on PATH.  Override with -DWINDEPLOYQT_EXECUTABLE=<path>.")
    endif()
  endif()

  message(STATUS "PlatformEditor: Qt6 ${Qt6_VERSION} found -- platedit_qt enabled")
else()
  message(STATUS "PlatformEditor: Qt6 not found -- platedit_qt skipped (core/TUI unaffected)")
endif()

enable_testing()
# The manifest corpus lives at the EmulatR source root (this dir's
# grandparent); override with -DPLATEDIT_MANIFEST_DIR for other layouts.
if(NOT DEFINED PLATEDIT_MANIFEST_DIR)
  get_filename_component(PLATEDIT_MANIFEST_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
add_test(NAME platedit_roundtrip
  COMMAND platedit_roundtrip_test
    "${PLATEDIT_MANIFEST_DIR}/ds10_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/ds25_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/es40_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/es45_v7_3_platform.json")

add_test(NAME platedit_schema
  COMMAND platedit_schema_test "${CMAKE_CURRENT_SOURCE_DIR}/schema/platform_schema.json")

add_test(NAME platedit_catalog
  COMMAND platedit_catalog_test
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/device_catalog.json"
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json")

add_test(NAME platedit_view
  COMMAND platedit_view_test
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/platform_schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/device_catalog.json")

add_test(NAME platedit_structural
  COMMAND platedit_structural_test "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json")
