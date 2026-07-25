# ============================================================================
# PlatformEditor -- EmulatR manifest authoring/validation tool
# ============================================================================
# T-01 scope: the Qt-free DOM + ordered/format-preserving JSON I/O core, plus
# its round-trip acceptance test.  No Qt dependency here by design (Section 5.1):
# the core is headless and unit-testable anywhere.  The Qt Widgets presentation
# layer is added in later tickets as a separate target.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(PlatformEditor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
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
  set_target_properties(platedit_qt PROPERTIES AUTOMOC ON)
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
  message(STATUS "PlatformEditor: Qt6 ${Qt6_VERSION} found -- platedit_qt enabled")
else()
  message(STATUS "PlatformEditor: Qt6 not found -- platedit_qt skipped (core/TUI unaffected)")
endif()

# --- Round-trip test (T-01 gate) --------------------------------------------
add_executable(roundtrip_test tests/roundtrip_test.cpp)
target_link_libraries(roundtrip_test PRIVATE platedit_core)

# --- Schema/policy test (T-02 gate) -----------------------------------------
add_executable(schema_test tests/schema_test.cpp)
target_link_libraries(schema_test PRIVATE platedit_core)

# --- Device catalog test (T-03 gate) ----------------------------------------
add_executable(catalog_test tests/catalog_test.cpp)
target_link_libraries(catalog_test PRIVATE platedit_core)

# --- TUI view-model + renderer test -----------------------------------------
add_executable(view_test tests/view_test.cpp)
target_link_libraries(view_test PRIVATE platedit_core)

# --- Structural-writer test (T-08 gate) --------------------------------------
add_executable(structural_test tests/structural_test.cpp)
target_link_libraries(structural_test PRIVATE platedit_core)

# --- TUI application --------------------------------------------------------
add_executable(platedit_tui tui/main.cpp)
target_link_libraries(platedit_tui PRIVATE platedit_core)

enable_testing()
# The manifest corpus lives in the EmulatR tree; pass its path in via
# PLATEDIT_MANIFEST_DIR (defaults to the source-relative sample).
if(NOT DEFINED PLATEDIT_MANIFEST_DIR)
  set(PLATEDIT_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()
add_test(NAME roundtrip
  COMMAND roundtrip_test
    "${PLATEDIT_MANIFEST_DIR}/ds10_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/ds25_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/es40_v7_3_platform.json"
    "${PLATEDIT_MANIFEST_DIR}/es45_v7_3_platform.json")

add_test(NAME schema
  COMMAND schema_test "${CMAKE_CURRENT_SOURCE_DIR}/schema/platform_schema.json")

add_test(NAME catalog
  COMMAND catalog_test
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/device_catalog.json"
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json")

add_test(NAME view
  COMMAND view_test
    "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/schema/platform_schema.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog/device_catalog.json")

add_test(NAME structural
  COMMAND structural_test "${PLATEDIT_MANIFEST_DIR}/ds20_v7_3_platform.json")
