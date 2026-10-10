# C:\workspace\Stella AI Studio\CMakeLists.txt

cmake_minimum_required(VERSION 3.22)

# C is required: JUCE 9.0.1 builds its bundled zlib/png/jpeg/flac in C mode.
project(StellaAIStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# JUCE 9
# ------------------------------------------------------------------------------
set(JUCE_DIR "C:/workspace/JUCE" CACHE PATH "JUCE 9 root folder")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Pass -DJUCE_DIR=<path to JUCE 9>.")
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

# ------------------------------------------------------------------------------
# The live preview's tools: Wasmtime (runs plugins in a sandbox) and wasi-sdk (compiles
# them to WebAssembly), with Export's compiler and plugin headers. Downloaded once and kept
# in the _deps folder beside build, so a new or deleted build folder doesn't fetch them again.
# ------------------------------------------------------------------------------
include(src/StellaToolchain.cmake)

# The studio's own images, compiled into the app.
juce_add_binary_data(StellaAssets
    HEADER_NAME StellaAssets.h
    NAMESPACE   StellaAssets
    SOURCES
        "assets/fanan logo.png")

# The Edit UI toolbox's primitives: one JSON file each in the primitives folder, compiled
# into the app. A new file there shows in the toolbox after the next build.
file(GLOB STELLA_PRIMITIVES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/primitives/*.json")

juce_add_binary_data(StellaPrimitives
    HEADER_NAME StellaPrimitives.h
    NAMESPACE   StellaPrimitives
    SOURCES     ${STELLA_PRIMITIVES})

# The Knob Studio's built-in looks for knobs, sliders and switches: KnobMaker layer documents
# (.fklayers) in the looks folder, compiled into the app. A new file there is offered after
# the next build.
file(GLOB STELLA_LOOKS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/looks/*.fklayers")

juce_add_binary_data(StellaLooks
    HEADER_NAME StellaLooks.h
    NAMESPACE   StellaLooks
    SOURCES     ${STELLA_LOOKS})

# The Stella runtime, the plugin wrappers, the programmed GUI elements' runtime with its
# font (Inter, SIL Open Font License: fonts/inter_license.txt) and Stella AI's guide, carried
# inside the app: the runtimes and wrappers are compiled into each plugin, not into the studio.
juce_add_binary_data(StellaRuntimeData
    HEADER_NAME StellaRuntimeData.h
    NAMESPACE   StellaRuntimeData
    SOURCES
        src/stella_api.h
        src/stella_runtime.h
        src/stella_runtime.cpp
        src/stella_clap.cpp
        src/stella_vst3.cpp
        src/stella_standalone.cpp
        src/stella_gui.h
        src/stella_gui.cpp
        src/stella_gui_win32.cpp
        src/stella_keys.h
        src/stb_image.h
        src/stella_element_api.h
        src/stella_element_host.h
        src/stella_elements.h
        src/stella_elements.cpp
        src/stb_truetype.h
        fonts/inter_regular.ttf
        fonts/inter_bold.ttf
        src/stella_builder_guide.md)

# ------------------------------------------------------------------------------
# App
# The platform itself is built with JUCE. The plugins it creates are not: they are
# built on CLAP (+ a VST3 wrapper) and contain no JUCE code.
# ------------------------------------------------------------------------------
juce_add_gui_app(StellaAIStudio
    PRODUCT_NAME "Stella AI Studio"
    COMPANY_NAME "Fanan"
    BUNDLE_ID    "com.fanan.stellaaistudio"
    VERSION      "${PROJECT_VERSION}")

target_sources(StellaAIStudio PRIVATE
    src/Main.cpp
    src/StellaLookAndFeel.h
    src/StellaLookAndFeel.cpp
    src/Settings.h
    src/Settings.cpp
    src/Project.h
    src/Project.cpp
    src/AudioEngine.h
    src/AudioEngine.cpp
    src/TopBar.h
    src/TopBar.cpp
    src/ChatPanel.h
    src/ChatPanel.cpp
    src/FananServer.h
    src/FananServer.cpp
    src/StellaAi.h
    src/StellaAi.cpp
    src/WasmPlugin.h
    src/WasmPlugin.cpp
    src/WasmElements.h
    src/WasmElements.cpp
    src/stella_element_host.h
    src/WasmCompiler.h
    src/WasmCompiler.cpp
    src/LivePreview.h
    src/LivePreview.cpp
    src/ProjectTools.h
    src/ProjectTools.cpp
    src/PluginExporter.h
    src/PluginExporter.cpp
    src/History.h
    src/History.cpp
    src/PresetBank.h
    src/PresetBank.cpp
    src/SchematicView.h
    src/SchematicView.cpp
    src/GuiLayout.h
    src/GuiLayout.cpp
    src/stella_keys.h
    src/PluginCanvas.h
    src/PluginCanvas.cpp
    src/PluginCanvasMenu.h
    src/PluginCanvas_Menu.cpp
    src/PluginCanvas_Mouse.cpp
    src/PluginCanvas_Elements.cpp
    src/Primitives.h
    src/Primitives.cpp
    src/Toolbox.h
    src/Toolbox.cpp
    src/ColourSwatch.h
    src/ColourSwatch.cpp
    src/UiHelpers.h
    src/Looks.h
    src/Looks.cpp
    src/LayerDoc.h
    src/LayerDoc.cpp
    src/LayerRender.h
    src/LayerRender.cpp
    src/LayerPanels.h
    src/LayerPanels.cpp
    src/LayerWorkspace.h
    src/LayerWorkspace.cpp
    src/BevelDialog.h
    src/BevelDialog.cpp
    src/TextureLibrary.h
    src/TextureLibrary.cpp
    src/BmpImage.h
    src/BmpImage.cpp
    src/KnobFile.h
    src/KnobFile.cpp
    src/GalleryBrowser.h
    src/GalleryBrowser.cpp
    src/ShapeDoc.h
    src/ShapeDoc.cpp
    src/ShapeEditor.h
    src/ShapeEditor.cpp
    src/ShapeWorkspace.h
    src/ShapeWorkspace.cpp
    src/KnobStudio.h
    src/KnobStudio.cpp
    src/Workspace.h
    src/Workspace.cpp
    src/KeyboardStrip.h
    src/KeyboardStrip.cpp
    src/MainComponent.h
    src/MainComponent.cpp)

target_include_directories(StellaAIStudio PRIVATE
    src
    "${STELLA_GENERATED_DIR}")

# Wasmtime's headers are someone else's code: their warnings aren't ours.
target_include_directories(StellaAIStudio SYSTEM PRIVATE "${STELLA_WASMTIME_DIR}/include")

target_compile_definitions(StellaAIStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StellaAIStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StellaAIStudio,JUCE_VERSION>")

if(WIN32)
    # JUCE 9 ships its own ASIO headers, so no external ASIO SDK path is needed.
    target_compile_definitions(StellaAIStudio PRIVATE JUCE_ASIO=1)
endif()

target_link_libraries(StellaAIStudio
    PRIVATE
        StellaRuntimeData
        StellaAssets
        StellaPrimitives
        StellaLooks
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_cryptography
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(WIN32)
    # Wasmtime as a DLL (its static library is a known source of crashes on Windows),
    # copied next to the app inside the build folder.
    target_link_libraries(StellaAIStudio PRIVATE "${STELLA_WASMTIME_DIR}/lib/wasmtime.dll.lib")
    add_custom_command(TARGET StellaAIStudio POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STELLA_WASMTIME_DIR}/lib/wasmtime.dll" "$<TARGET_FILE_DIR:StellaAIStudio>"
        VERBATIM)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(StellaAIStudio PRIVATE "${STELLA_WASMTIME_DIR}/lib/libwasmtime.a" Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# Everything the build makes stays inside the build folder; the only thing outside it is the
# _deps folder of downloaded tools. Nothing is installed or copied anywhere else.
