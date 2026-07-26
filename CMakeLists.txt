cmake_minimum_required(VERSION 3.10)
project(CowEngine)

set(CMAKE_CXX_STANDARD 17)

# FetchContent used to pull header-only dependencies when not available
include(FetchContent)

# Build modes:
#   BUILD_GAME=OFF (default): build the editor (CowEngine) with full ImGui editor UI
#   BUILD_GAME=ON           : build a standalone shippable game (no editor, assets bundled)
option(BUILD_GAME "Build a standalone game (no editor, asset bundle)" OFF)
if(BUILD_GAME)
    message(STATUS "BUILD_GAME=ON — building standalone game (no editor)")
endif()

# BUILD_SERVER=ON builds ONLY the headless authoritative server: it reuses the
# engine simulation (Scene/ECS/Bullet/.cow) with a no-op GL shim and links no
# windowing/render/UI deps. Native-only.
option(BUILD_SERVER "Build the headless authoritative game server" OFF)

# Helper target: build both native and web game artifacts used by the embedding
# pipeline. Invokes tools/build/build-all-games.sh from the source root. This
# target is intentionally a convenience wrapper so CI or local devs can run:
#   cmake --build <build-dir> --target build_all_games
add_custom_target(build_all_games
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tools/build/build-all-games.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Build native and web game outputs (dist/game-native, dist/game-web)"
)

# vcpkg support guidance
option(USE_VCPKG "Enable vcpkg usage (requires providing CMAKE_TOOLCHAIN_FILE at configure time)" ON)
if(EMSCRIPTEN)
    # ensure vcpkg checks do not run for web builds
    set(USE_VCPKG OFF CACHE BOOL "Enable vcpkg usage (requires providing CMAKE_TOOLCHAIN_FILE at configure time)" FORCE)
endif()
if(USE_VCPKG)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR "USE_VCPKG is ON but CMAKE_TOOLCHAIN_FILE is not set. Configure CMake with -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake")
    endif()
    if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        message(WARNING "CMAKE_TOOLCHAIN_FILE does not look like a vcpkg toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
    endif()
    message(STATUS "vcpkg toolchain in use: ${CMAKE_TOOLCHAIN_FILE}")
    set(VCPKG_SUGGESTED_PACKAGES "glfw3 glm nlohmann-json bullet3 imgui glad")
    message(STATUS "If you haven't already, install dependencies with vcpkg: ${VCPKG_SUGGESTED_PACKAGES}")
endif()

# Core runtime sources: compiled into EVERY target (editor and standalone).
# Nothing in this list may include or link Dear ImGui / ImGuizmo — that is what
# makes ENGINE_WITH_EDITOR=0 builds free of the UI dependency.
set(SOURCES
    src/app/main.cpp
    src/render/Shader.cpp
    src/meshes/PlaneMesh.cpp
    src/meshes/CubeMesh.cpp
    src/meshes/CircleMesh.cpp
    src/meshes/ShockwaveMesh.cpp
    src/meshes/StaticMesh.cpp
    src/meshes/AssetManager.cpp
    src/ecs/Factories.cpp
    src/ecs/ComponentOps.cpp
    src/ecs/systems/PhysicsSyncSystem.cpp
    src/ecs/systems/RenderSystem.cpp
    src/ecs/systems/NametagSystem.cpp
    src/ecs/systems/BlastVfxSystem.cpp
    src/ecs/systems/ScriptSystem.cpp
    src/ecs/systems/PlayerInputSystem.cpp
    src/ecs/systems/LocalInputSystem.cpp
    src/core/Scene.cpp
    src/core/PhysicsWorld.cpp
    src/core/Camera.cpp
    src/platform/Window.cpp
    src/app/Application.cpp
    # In-game settings overlay. Lives in the shared list because the standalone
    # game targets are built from ${SOURCES} alone; the editor compiles it too
    # and simply never references it, since it configures VFX through ImGui.
    src/app/GameMenu.cpp
    src/render/PostFX.cpp
    src/render/TextRenderer.cpp
    src/script/CowScript.cpp
    src/script/ScriptHost.cpp
    src/net/Protocol.cpp
    src/net/NetClient.cpp
)

# The one font TextRenderer rasterises its glyph atlas from. Unlike the rest of
# engine_assets/ (editor-only), this ships with EVERY rendering target — a game
# build draws player nametags with it — so each target below stages this single
# file at the same relative path the engine looks it up under.
set(ENGINE_FONT_DIR "engine_assets/fonts/JetBrainsMono-2.304/fonts/ttf")
set(ENGINE_FONT_FILE "JetBrainsMono-Regular.ttf")
set(ENGINE_FONT_PATH "${ENGINE_FONT_DIR}/${ENGINE_FONT_FILE}")

# Editor-only sources: compiled and linked ONLY when ENGINE_WITH_EDITOR=1.
# Everything here either draws the editor UI, drives the Build/packaging
# pipeline, or otherwise depends on Dear ImGui / ImGuizmo. A standalone game
# build (BUILD_GAME=ON) never adds these to the compilation, so the UI library
# is fully stripped from the shipped binary.
set(SOURCES_EDITOR_ONLY
    src/app/GameBuilder.cpp
    src/app/EditorUI.cpp
    src/app/CodeEditor.cpp
    src/editor/EditorContext.cpp
    src/editor/Win95Widgets.cpp
    src/editor/panels/WorkspacePanel.cpp
    src/editor/panels/HierarchyPanel.cpp
    src/editor/panels/InspectorPanel.cpp
    src/editor/panels/ConsolePanel.cpp
    src/editor/panels/StatsPanel.cpp
    src/editor/panels/RuntimePanel.cpp
    src/editor/panels/FileBrowserPanel.cpp
    src/editor/panels/VfxPanel.cpp
    src/render/ColliderDebugDrawer.cpp
    src/core/InputHandler.cpp
    src/platform/ImGuiLayer.cpp
    src/platform/imgui_impl_emscripten.cpp
)

# ===== Headless server =====
# When BUILD_SERVER=ON this is the ONLY target produced. It compiles the sim
# sources with ENGINE_HEADLESS (which swaps GL/GLFW headers for the no-op shim)
# and links just the simulation libraries — no glfw/glad/OpenGL/ImGui.
if(BUILD_SERVER)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "BUILD_SERVER is native-only; do not configure it with emcmake.")
    endif()

    # Headless simulation sources shared by the server and the netclient test.
    set(SIM_SOURCES
        src/core/Scene.cpp
        src/core/PhysicsWorld.cpp
        src/core/Camera.cpp
        src/ecs/Factories.cpp
        src/ecs/ComponentOps.cpp
        src/ecs/systems/PhysicsSyncSystem.cpp
        src/ecs/systems/ScriptSystem.cpp
        src/meshes/PlaneMesh.cpp
        src/meshes/CubeMesh.cpp
        src/meshes/CircleMesh.cpp
        src/meshes/StaticMesh.cpp
        src/meshes/AssetManager.cpp
        src/script/CowScript.cpp
        src/script/ScriptHost.cpp
        src/net/Protocol.cpp
    )
    set(SERVER_SOURCES
        ${SIM_SOURCES}
        src/net/UdpLink.cpp
        src/server/GameServer.cpp
        src/server/main.cpp
    )

    find_package(Bullet REQUIRED)
    find_package(Bullet CONFIG REQUIRED)
    find_package(glm CONFIG QUIET)
    if(NOT glm_FOUND)
        FetchContent_Declare(glm_repo GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG master)
        FetchContent_MakeAvailable(glm_repo)
    endif()
    find_package(nlohmann_json CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        FetchContent_Declare(nlohmann_json_repo GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0)
        FetchContent_MakeAvailable(nlohmann_json_repo)
    endif()
    find_package(EnTT CONFIG QUIET)
    if(NOT EnTT_FOUND)
        FetchContent_Declare(entt_repo GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.13.2)
        FetchContent_MakeAvailable(entt_repo)
    endif()

    add_executable(CowEngineServer ${SERVER_SOURCES})
    target_compile_definitions(CowEngineServer PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(CowEngineServer PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(CowEngineServer PRIVATE
        glm::glm
        nlohmann_json::nlohmann_json
        BulletDynamics
        BulletCollision
        LinearMath
        EnTT::EnTT
    )

    # Stage the assets the server loads at runtime (scene + scripts + models).
    add_custom_command(TARGET CowEngineServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scenes $<TARGET_FILE_DIR:CowEngineServer>/scenes
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:CowEngineServer>/scripts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/models $<TARGET_FILE_DIR:CowEngineServer>/models
    )
    install(TARGETS CowEngineServer RUNTIME DESTINATION .)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/scenes/  DESTINATION scenes)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/ DESTINATION scripts)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/models/  DESTINATION models)

    # Headless integration test for the client netcode (NetClient) driven by a
    # mock transport — verifies remote-avatar interpolation, join/leave, and
    # local reconciliation without a GL context. Built alongside the server.
    add_executable(cowengine_netclient_test
        ${SIM_SOURCES}
        src/net/NetClient.cpp
        test/netclient_test.cpp
    )
    target_compile_definitions(cowengine_netclient_test PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(cowengine_netclient_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(cowengine_netclient_test PRIVATE
        glm::glm nlohmann_json::nlohmann_json
        BulletDynamics BulletCollision LinearMath EnTT::EnTT)

    # Headless test for runtime script attachment (attach_script) and the
    # per-object lifetimes it enables — see test/script_spawn_test.cpp.
    add_executable(cowengine_script_spawn_test
        ${SIM_SOURCES}
        test/script_spawn_test.cpp
    )
    target_compile_definitions(cowengine_script_spawn_test PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(cowengine_script_spawn_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(cowengine_script_spawn_test PRIVATE
        glm::glm nlohmann_json::nlohmann_json
        BulletDynamics BulletCollision LinearMath EnTT::EnTT)

    # Headless test for server-side player names: sanitisation of the name in a
    # ClientHello and who gets told about whom — see test/server_names_test.cpp.
    add_executable(cowengine_server_names_test
        ${SIM_SOURCES}
        src/server/GameServer.cpp
        test/server_names_test.cpp
    )
    target_compile_definitions(cowengine_server_names_test PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(cowengine_server_names_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(cowengine_server_names_test PRIVATE
        glm::glm nlohmann_json::nlohmann_json
        BulletDynamics BulletCollision LinearMath EnTT::EnTT)

    # Repro harness for repeated Despawn+Spawn cycles on NetClient, as produced
    # by repeated scene resets -- see test/netclient_reset_repeat_test.cpp.
    add_executable(cowengine_netclient_reset_repeat_test
        ${SIM_SOURCES}
        src/net/NetClient.cpp
        test/netclient_reset_repeat_test.cpp
    )
    target_compile_definitions(cowengine_netclient_reset_repeat_test PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(cowengine_netclient_reset_repeat_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(cowengine_netclient_reset_repeat_test PRIVATE
        glm::glm nlohmann_json::nlohmann_json
        BulletDynamics BulletCollision LinearMath EnTT::EnTT)

    # Repro harness for repeated scene resets (reset pressure plate) in
    # multiplayer -- see test/reset_repeat_test.cpp.
    add_executable(cowengine_reset_repeat_test
        ${SIM_SOURCES}
        src/server/GameServer.cpp
        test/reset_repeat_test.cpp
    )
    target_compile_definitions(cowengine_reset_repeat_test PRIVATE
        ENGINE_HEADLESS=1 ENGINE_WITH_EDITOR=0 GLM_ENABLE_EXPERIMENTAL)
    target_include_directories(cowengine_reset_repeat_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    target_link_libraries(cowengine_reset_repeat_test PRIVATE
        glm::glm nlohmann_json::nlohmann_json
        BulletDynamics BulletCollision LinearMath EnTT::EnTT)

    # Server + test are the only artifacts for this configuration.
    return()
endif()

if(NOT EMSCRIPTEN)
    add_executable(CowEngine ${SOURCES})
    if(NOT BUILD_GAME)
        target_sources(CowEngine PRIVATE ${SOURCES_EDITOR_ONLY})
    endif()
endif()

if(NOT EMSCRIPTEN)
    find_package(Bullet REQUIRED)
    find_package(Bullet CONFIG REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glad CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

# GLM and nlohmann_json: prefer package configs, but provide simple FetchContent
# fallbacks for web builds (or when the package isn't available).
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM package not found; will attempt to fetch via FetchContent")
    FetchContent_Declare(
        glm_repo
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(glm_repo)
endif()

find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json package not found; will attempt to fetch via FetchContent")
    FetchContent_Declare(
        nlohmann_json_repo
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
    )
    FetchContent_MakeAvailable(nlohmann_json_repo)
endif()

# EnTT: header-only ECS. Available via vcpkg on native; FetchContent for web/fallback.
find_package(EnTT CONFIG QUIET)
if(NOT EnTT_FOUND)
    message(STATUS "EnTT package not found; will attempt to fetch via FetchContent")
    FetchContent_Declare(
        entt_repo
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG v3.13.2
    )
    FetchContent_MakeAvailable(entt_repo)
endif()

# Link imported targets provided by vcpkg / config packages
if(NOT EMSCRIPTEN)
    # ImGui / ImGuizmo are an EDITOR-ONLY dependency. For a standalone game
    # build (BUILD_GAME=ON) we skip fetching and building them entirely, so the
    # UI library never enters the compile or link pipeline.
    if(NOT BUILD_GAME)
        # Native ImGui docking build
        FetchContent_Declare(
            imgui_repo
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG docking
        )
        FetchContent_MakeAvailable(imgui_repo)

        add_library(imgui_native STATIC
            ${imgui_repo_SOURCE_DIR}/imgui.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_repo_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_repo_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(imgui_native PUBLIC ${imgui_repo_SOURCE_DIR} ${imgui_repo_SOURCE_DIR}/backends)
        target_compile_definitions(imgui_native PRIVATE IMGUI_ENABLE_DOCKING)
        target_link_libraries(imgui_native PUBLIC glfw glad::glad)

        FetchContent_Declare(
            imguizmo_repo
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
            GIT_TAG master
        )
        FetchContent_GetProperties(imguizmo_repo)
        if(NOT imguizmo_repo_POPULATED)
            FetchContent_Populate(imguizmo_repo)
        endif()

        add_library(imguizmo_native STATIC ${imguizmo_repo_SOURCE_DIR}/src/ImGuizmo.cpp)
        target_include_directories(imguizmo_native PUBLIC ${imguizmo_repo_SOURCE_DIR} ${imguizmo_repo_SOURCE_DIR}/src ${imgui_repo_SOURCE_DIR})
        target_link_libraries(imguizmo_native PUBLIC imgui_native)
        target_compile_definitions(imguizmo_native PRIVATE IMGUI_ENABLE_DOCKING)
    endif()

    # Common runtime link libraries (shared by editor and standalone builds).
    target_link_libraries(CowEngine PRIVATE
        glad::glad
        glfw
        glm::glm
        OpenGL::GL
        nlohmann_json::nlohmann_json
        BulletDynamics
        BulletCollision
        LinearMath
        EnTT::EnTT
    )
    if(BUILD_GAME)
        # Standalone game: no editor, no ImGui. Asset paths resolve relative to the exe.
        target_compile_definitions(CowEngine PRIVATE ENGINE_WITH_EDITOR=0 COWENGINE_GAME GLM_ENABLE_EXPERIMENTAL)
    else()
        # Editor build: link the UI library and enable ImGui + docking.
        target_link_libraries(CowEngine PRIVATE imgui_native imguizmo_native)
        target_compile_definitions(CowEngine PRIVATE ENGINE_WITH_EDITOR=1 USE_IMGUI ASSET_ROOT="${CMAKE_SOURCE_DIR}" GLM_ENABLE_EXPERIMENTAL IMGUI_ENABLE_DOCKING)
    endif()
    target_include_directories(CowEngine PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/glad/include ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
    if(NOT BUILD_GAME)
        target_include_directories(CowEngine PRIVATE ${imguizmo_repo_SOURCE_DIR})
    endif()

    # Always stage assets next to the executable so the game (and editor) can
    # be launched directly from the build directory.
    add_custom_command(TARGET CowEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/src/shaders $<TARGET_FILE_DIR:CowEngine>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scenes $<TARGET_FILE_DIR:CowEngine>/scenes
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:CowEngine>/scripts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/models $<TARGET_FILE_DIR:CowEngine>/models
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/${ENGINE_FONT_PATH} $<TARGET_FILE_DIR:CowEngine>/${ENGINE_FONT_PATH}
    )

    # `cmake --install` lays down a redistributable folder containing the exe
    # and all assets it needs at runtime. No external files required.
    install(TARGETS CowEngine RUNTIME DESTINATION .)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/shaders/ DESTINATION shaders)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/scenes/      DESTINATION scenes)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/     DESTINATION scripts)
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/models/      DESTINATION models)
    if(NOT BUILD_GAME)
        # The editor build still needs the engine asset folder (fonts, icons,
        # editor scripts). The standalone game build only needs the text font.
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/engine_assets/ DESTINATION engine_assets)
    else()
        install(FILES ${CMAKE_SOURCE_DIR}/${ENGINE_FONT_PATH} DESTINATION ${ENGINE_FONT_DIR})
    endif()

    # ===== Embedded game template (native editor only) =====
    # The editor's Build menu writes a standalone game folder by extracting
    # an embedded template. To produce that template, build a second
    # executable from the same sources with COWENGINE_GAME defined, then run
    # the bin2c host tool to bake the resulting files into a generated C++
    # source linked into the editor.
    if(NOT BUILD_GAME)
        # The template IS a standalone game, so it is decoupled exactly like a
        # BUILD_GAME=ON build: ENGINE_WITH_EDITOR=0, no ImGui, and it reuses only
        # ${SOURCES} — the editor-only sources are deliberately excluded.
        add_executable(CowEngineGameTpl ${SOURCES})
        set_target_properties(CowEngineGameTpl PROPERTIES OUTPUT_NAME "CowEngineGame")
        target_compile_definitions(CowEngineGameTpl PRIVATE
            ENGINE_WITH_EDITOR=0 COWENGINE_GAME GLM_ENABLE_EXPERIMENTAL)
        target_include_directories(CowEngineGameTpl PRIVATE
            ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/glad/include
            ${BULLET_INCLUDE_DIRS} /usr/include/bullet)
        target_link_libraries(CowEngineGameTpl PRIVATE
            glad::glad glfw glm::glm OpenGL::GL nlohmann_json::nlohmann_json
            BulletDynamics BulletCollision LinearMath
            EnTT::EnTT)

        # Stage the game template (exe + assets it needs at runtime) under the
        # build dir. bin2c walks this directory and emits a C++ array per file.
        set(GAME_TEMPLATE_DIR ${CMAKE_BINARY_DIR}/game_template_native)
        add_custom_command(TARGET CowEngineGameTpl POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${GAME_TEMPLATE_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GAME_TEMPLATE_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:CowEngineGameTpl> ${GAME_TEMPLATE_DIR}/$<TARGET_FILE_NAME:CowEngineGameTpl>
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shaders ${GAME_TEMPLATE_DIR}/shaders
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scenes       ${GAME_TEMPLATE_DIR}/scenes
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/scripts      ${GAME_TEMPLATE_DIR}/scripts
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/models       ${GAME_TEMPLATE_DIR}/models
            COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/${ENGINE_FONT_PATH} ${GAME_TEMPLATE_DIR}/${ENGINE_FONT_PATH}
            VERBATIM)

        # bin2c is a tiny C++17 host tool — built with the same compiler as
        # the editor (always native here; this whole block is gated on
        # NOT EMSCRIPTEN).
        add_executable(cowengine_bin2c tools/embed/bin2c.cpp)
        set_target_properties(cowengine_bin2c PROPERTIES
            CXX_STANDARD 17 OUTPUT_NAME "bin2c")

        if(WIN32)
            set(_native_slice "native-windows")
        else()
            set(_native_slice "native-linux")
        endif()

        set(EMBEDDED_CPP ${CMAKE_BINARY_DIR}/generated/EmbeddedTemplate.cpp)
        add_custom_command(
            OUTPUT ${EMBEDDED_CPP}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
            COMMAND $<TARGET_FILE:cowengine_bin2c> ${EMBEDDED_CPP}
                --slice ${_native_slice} ${GAME_TEMPLATE_DIR}
                --slice web ${CMAKE_SOURCE_DIR}/dist/game-web
            DEPENDS CowEngineGameTpl cowengine_bin2c
            VERBATIM)
        target_sources(CowEngine PRIVATE ${EMBEDDED_CPP})
    endif()
else()
    # Web build: only link header-only / portable libs; concrete JS/Web APIs (Ammo.js, ImGui port) handled at runtime
    # Create a minimal CowEngine target alias for common includes if needed
    # (CowEngine_web will be the actual web executable)
    add_library(cowengine_common INTERFACE)
    target_link_libraries(cowengine_common INTERFACE glm::glm nlohmann_json::nlohmann_json)
    # make sure web target can see glad headers if present locally
endif()

# Web (Emscripten) target --------------------------------------------------
if(EMSCRIPTEN)
    # For Release-style builds, prefer size over speed across every Emscripten
    # target — Bullet, ImGui, and our own code — so -flto can actually do
    # whole-program work at link time.
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        string(REGEX REPLACE "-O[0-9s]" "-Oz" CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE}")
        string(REGEX REPLACE "-O[0-9s]" "-Oz" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        if(NOT CMAKE_C_FLAGS_RELEASE MATCHES "-Oz")
            set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} -Oz")
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Oz")
        endif()
        if(NOT CMAKE_C_FLAGS_RELEASE MATCHES "-flto")
            set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} -flto")
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
        endif()
    endif()

    # Use the same source list for web builds (unified codebase), plus the
    # browser transport bridge (EM_JS <-> window.CowNet), which is web-only.
    set(WEB_SOURCES ${SOURCES} src/net/WebClientTransport.cpp)

    if(NOT BUILD_GAME)
        list(APPEND WEB_SOURCES ${SOURCES_EDITOR_ONLY})
        list(APPEND WEB_SOURCES src/templates/EmbeddedTemplate_web.cpp)
    endif()


    # Ensure Bullet is available for web builds by fetching and building it
    # under the Emscripten toolchain so the same physics API works on web.
    # Configure a minimal Bullet build suitable for linking into the web target.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
    # Keep Bullet's archives out of the install tree — they're statically linked
    # into the wasm; shipping them again as .a / headers wastes ~5MB.
    set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
    set(INSTALL_EXTRA_LIBS OFF CACHE BOOL "" FORCE)
    set(INSTALL_CMAKE_FILES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        bullet_repo
        GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(bullet_repo)

    # Fetch Dear ImGui for web builds and build it into the wasm — editor only.
    # A standalone web game (BUILD_GAME=ON) never fetches, compiles, or links it.
    if(NOT BUILD_GAME)
        FetchContent_Declare(
            imgui_repo
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG docking
        )
        FetchContent_MakeAvailable(imgui_repo)
    endif()

    add_executable(CowEngine_web ${WEB_SOURCES})

    # Keep most headers/includes and definitions consistent
    target_include_directories(CowEngine_web PRIVATE ${CMAKE_SOURCE_DIR}/include ${bullet_repo_SOURCE_DIR}/src)
    target_include_directories(CowEngine_web PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(BUILD_GAME)
        # Standalone web game: no editor, no ImGui.
        target_compile_definitions(CowEngine_web PRIVATE ENGINE_WITH_EDITOR=0 COWENGINE_GAME GLM_ENABLE_EXPERIMENTAL EMSCRIPTEN)
    else()
        target_compile_definitions(CowEngine_web PRIVATE ENGINE_WITH_EDITOR=1 ASSET_ROOT="${CMAKE_SOURCE_DIR}" GLM_ENABLE_EXPERIMENTAL EMSCRIPTEN IMGUI_ENABLE_DOCKING)
    endif()

    # Link libraries including Bullet built for EMSCRIPTEN so we use the
    # same native physics API on web and desktop.
    target_link_libraries(CowEngine_web PRIVATE
        glm::glm
        nlohmann_json::nlohmann_json
        BulletDynamics
        BulletCollision
        LinearMath
        EnTT::EnTT
    )

    # Emscripten linker flags: WebGL2, modest memory cap (4GB was massive
    # overkill — wasm32 can address up to 4GB, but a real game-in-browser will
    # OOM the tab long before that). ASSERTIONS off in Release for code size.
    # Memory is fixed (not growable): a growable WebAssembly.Memory backs its
    # buffer with a resizable ArrayBuffer, and browsers reject passing views
    # of those to TextDecoder.decode() (used by emscripten's UTF8ToString),
    # crashing with "can't be a resizable ArrayBuffer".
    set(WEB_LINK_FLAGS "-s USE_WEBGL2=1 -s INITIAL_MEMORY=1GB -s USE_GLFW=3 -s ENVIRONMENT=web")
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        # Skip --closure 1 (ADVANCED mode): our EM_ASM blocks reference browser
        # globals like localStorage directly, and Closure rewrites them away.
        # Would need a custom externs file to safely re-enable.
        list(APPEND WEB_LINK_FLAGS "-s ASSERTIONS=0" "-Oz" "-flto")
    else()
        list(APPEND WEB_LINK_FLAGS "-s ASSERTIONS=1")
    endif()
    # Export ccall (for Module.ccall in EM_ASM) and FS (so Module.FS is available in preRun)
    list(APPEND WEB_LINK_FLAGS "-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"FS\",\"UTF8ToString\",\"stringToUTF8\",\"lengthBytesUTF8\",\"HEAPU8\"]")
    # Networking bridge: window.CowNet mallocs a buffer, copies an inbound
    # message into HEAPU8, and calls _cow_net_deliver, then frees it.
    # Also exports the touch-control bridge the HTML shell's on-screen joystick
    # and look pad call into (see src/platform/Window.cpp).
    list(APPEND WEB_LINK_FLAGS "-sEXPORTED_FUNCTIONS=[\"_main\",\"_malloc\",\"_free\",\"_cow_net_deliver\",\"_cow_touch_key\",\"_cow_touch_look\",\"_cow_touch_active\"]")
    list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/scenes@/scenes")
    list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/src/shaders@/shaders")
    list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/models@/models")
    list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/scripts@/scripts")
    # The bundled JetBrainsMono download has 7.5MB of TTF variants + 2MB of
    # webfonts, so weights are listed one at a time rather than preloading the
    # folder. The regular weight goes into every web target — TextRenderer bakes
    # its glyph atlas from it; the editor adds two more for its ImGui UI.
    set(_jb_ttf "${CMAKE_SOURCE_DIR}/${ENGINE_FONT_DIR}")
    set(_mount "/${ENGINE_FONT_DIR}")
    list(APPEND WEB_LINK_FLAGS "--preload-file" "${_jb_ttf}/${ENGINE_FONT_FILE}@${_mount}/${ENGINE_FONT_FILE}")
    if(NOT BUILD_GAME)
        # Editor also needs help docs and a slim slice of icons.
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/src/templates/help.md@/src/templates/help.md")
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${_jb_ttf}/JetBrainsMono-Bold.ttf@${_mount}/JetBrainsMono-Bold.ttf")
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${_jb_ttf}/JetBrainsMono-SemiBold.ttf@${_mount}/JetBrainsMono-SemiBold.ttf")
        set(_icons "${CMAKE_SOURCE_DIR}/engine_assets/icons/png")
        set(_imount "/engine_assets/icons/png")
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${_icons}/up-down-left-right-solid-full.png@${_imount}/up-down-left-right-solid-full.png")
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${_icons}/rotate-solid-full.png@${_imount}/rotate-solid-full.png")
        list(APPEND WEB_LINK_FLAGS "--preload-file" "${_icons}/expand-solid-full.png@${_imount}/expand-solid-full.png")

        if(EXISTS "${CMAKE_SOURCE_DIR}/dist/game-web")
            list(APPEND WEB_LINK_FLAGS "--preload-file" "${CMAKE_SOURCE_DIR}/dist/game-web@/game_template_web")
        else()
            message(WARNING "Web editor build: dist/game-web not found; Build menu web export will be disabled.")
        endif()
    endif()

    # Join flags into a single string and attach to target
    string(JOIN " " WEB_LINK_FLAGS_STR ${WEB_LINK_FLAGS})
    # Emit only CowEngine.js (+ .wasm + .data) — no Emscripten-generated HTML
    # shell, since we copy our own index.html below. Avoids shipping two HTML
    # files with identical contents.
    set_target_properties(CowEngine_web PROPERTIES
        LINK_FLAGS "${WEB_LINK_FLAGS_STR}"
        OUTPUT_NAME "CowEngine"
        SUFFIX ".js"
    )

    # ImGui / ImGuizmo for the web target — EDITOR ONLY. Skipped entirely for a
    # standalone web game so the wasm carries none of the UI library.
    if(NOT BUILD_GAME)
        # Build a minimal imgui static library from the fetched repo so we can
        # link it into the web target. We include the core sources and the
        # OpenGL3 backend for rendering. Input is provided by a small
        # imgui_impl_emscripten platform file implemented in src/.
        add_library(imgui_web STATIC
            ${imgui_repo_SOURCE_DIR}/imgui.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_repo_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_repo_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        )
        target_include_directories(imgui_web PUBLIC ${imgui_repo_SOURCE_DIR} ${imgui_repo_SOURCE_DIR}/backends)
        target_compile_definitions(imgui_web PRIVATE IMGUI_ENABLE_DOCKING)

        FetchContent_Declare(
            imguizmo_repo
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
            GIT_TAG master
        )
        FetchContent_GetProperties(imguizmo_repo)
        if(NOT imguizmo_repo_POPULATED)
            FetchContent_Populate(imguizmo_repo)
        endif()

        add_library(imguizmo_web STATIC ${imguizmo_repo_SOURCE_DIR}/src/ImGuizmo.cpp)
        target_include_directories(imguizmo_web PUBLIC ${imguizmo_repo_SOURCE_DIR} ${imguizmo_repo_SOURCE_DIR}/src ${imgui_repo_SOURCE_DIR})
        target_link_libraries(imguizmo_web PUBLIC imgui_web)
        target_compile_definitions(imguizmo_web PRIVATE IMGUI_ENABLE_DOCKING)

        # Link imgui + imguizmo into the web executable and expose USE_IMGUI to compile-time
        target_link_libraries(CowEngine_web PRIVATE imgui_web imguizmo_web)
        target_compile_definitions(CowEngine_web PRIVATE USE_IMGUI)
        target_include_directories(CowEngine_web PRIVATE ${imguizmo_repo_SOURCE_DIR} ${imguizmo_repo_SOURCE_DIR}/src)
    endif()

    # Do not attempt to copy shaders at post-build for the web target (preloaded instead)
    # Copy the appropriate HTML shell into the web output as index.html.
    if(BUILD_GAME)
        set(CUSTOM_SHELL_SOURCE "${CMAKE_SOURCE_DIR}/src/templates/GameTemplate.html")
    else()
        set(CUSTOM_SHELL_SOURCE "${CMAKE_SOURCE_DIR}/src/templates/EditorTemplate.html")
    endif()
    add_custom_command(TARGET CowEngine_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CUSTOM_SHELL_SOURCE} $<TARGET_FILE_DIR:CowEngine_web>/index.html
    )

    # `cmake --install` ships the wasm + data + index.html as a single folder
    install(FILES
        $<TARGET_FILE_DIR:CowEngine_web>/CowEngine.js
        $<TARGET_FILE_DIR:CowEngine_web>/CowEngine.wasm
        $<TARGET_FILE_DIR:CowEngine_web>/CowEngine.data
        $<TARGET_FILE_DIR:CowEngine_web>/index.html
        ${CMAKE_SOURCE_DIR}/tools/build/web-instructions.txt
        DESTINATION .
    )
    install(PROGRAMS
        ${CMAKE_SOURCE_DIR}/tools/build/serve-web.sh
        DESTINATION .
    )
endif()
