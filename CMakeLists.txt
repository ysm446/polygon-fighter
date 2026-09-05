cmake_minimum_required(VERSION 3.24)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" PACKAGE_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/package.json")
string(JSON APP_VERSION GET "${PACKAGE_JSON}" version)
project(PolygonFighter VERSION ${APP_VERSION} LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Windows x64 and MSVC are required.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure with -A x64.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(FetchContent)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
# 初期版はAVX2を必須にせず、SSE2で動作させる。
foreach(FEATURE USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
    set(${FEATURE} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(Jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG 0373ec0dd762e4bc2f6acdb08371ee84fa23c6db
    SOURCE_SUBDIR Build)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06)
FetchContent_MakeAvailable(Jolt imgui)
# Joltの/Wall対象からWindows SDK等の外部ヘッダーを除外する。
target_compile_options(Jolt PRIVATE /external:anglebrackets /external:W0)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(imgui_backend SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui_backend PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(imgui_backend PUBLIC d3d12 dxgi d3dcompiler dwmapi)

add_library(fighter_combat STATIC src/combat/CombatSystem.cpp)
add_library(fighter_assets STATIC src/assets/CharacterAsset.cpp)
target_include_directories(fighter_assets PUBLIC src)
target_include_directories(fighter_assets SYSTEM PRIVATE third_party/cgltf)
target_compile_options(fighter_assets PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_include_directories(fighter_combat PUBLIC src)
target_compile_options(fighter_combat PRIVATE /W4 /permissive- /utf-8 /EHsc)
add_library(fighter_physics STATIC src/physics/PhysicsWorld.cpp src/physics/PhysicsRig.cpp src/animation/AnimationPlayer.cpp)
target_include_directories(fighter_physics PUBLIC src)
target_link_libraries(fighter_physics PRIVATE Jolt PUBLIC fighter_combat)

add_executable(polygon_fighter WIN32
    src/app/Main.cpp
    src/render/Renderer.cpp
    src/debug/DebugUI.cpp)
target_include_directories(polygon_fighter PRIVATE src)
target_compile_definitions(polygon_fighter PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(polygon_fighter PRIVATE fighter_assets fighter_physics imgui_backend d3d12 dxgi d3dcompiler)
foreach(TARGET fighter_physics polygon_fighter)
    target_compile_options(${TARGET} PRIVATE /W4 /permissive- /utf-8 /EHsc)
endforeach()
add_custom_target(polygon_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:polygon_fighter>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Primitive.hlsl"
        "$<TARGET_FILE_DIR:polygon_fighter>/shaders/Primitive.hlsl")
add_dependencies(polygon_fighter polygon_shaders)
add_custom_target(polygon_models
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:polygon_fighter>/assets/characters"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/models/male-fighter.glb"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/models/female-fighter.glb"
        "$<TARGET_FILE_DIR:polygon_fighter>/assets/characters")
add_dependencies(polygon_fighter polygon_models)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT polygon_fighter)

include(CTest)
if(BUILD_TESTING)
    add_executable(asset_tests tests/AssetTests.cpp)
    target_link_libraries(asset_tests PRIVATE fighter_assets fighter_physics)
    target_compile_definitions(asset_tests PRIVATE ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/models")
    target_compile_options(asset_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME character_assets_and_combat COMMAND asset_tests)
    add_executable(physics_tests tests/PhysicsTests.cpp)
    target_link_libraries(physics_tests PRIVATE fighter_physics)
    target_compile_options(physics_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME physics_drop_reset_and_timestep COMMAND physics_tests)
    add_executable(humanoid_tests tests/HumanoidTests.cpp)
    target_link_libraries(humanoid_tests PRIVATE fighter_physics)
    target_compile_options(humanoid_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME humanoid_standing_and_recovery COMMAND humanoid_tests)
    add_executable(animation_tests tests/AnimationTests.cpp)
    target_link_libraries(animation_tests PRIVATE fighter_physics)
    target_compile_options(animation_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME physical_punch_and_animation COMMAND animation_tests)
    add_executable(combat_tests tests/CombatTests.cpp)
    target_link_libraries(combat_tests PRIVATE fighter_combat)
    target_compile_options(combat_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME combat_rules COMMAND combat_tests)
    add_executable(duel_tests tests/DuelTests.cpp)
    target_link_libraries(duel_tests PRIVATE fighter_physics)
    target_compile_options(duel_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    add_test(NAME physical_duel COMMAND duel_tests)
    add_test(NAME dx12_smoke COMMAND polygon_fighter --smoke-test)
    set_tests_properties(dx12_smoke PROPERTIES TIMEOUT 45)
endif()
