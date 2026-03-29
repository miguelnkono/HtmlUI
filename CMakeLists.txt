cmake_minimum_required(VERSION 3.20)
project(htmlui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Strict warnings — treat the codebase as clean from the start
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter    # common in stub implementations
        -Wno-missing-field-initializers
    )
endif()

# Build type default
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")

option(HTMLUI_BUILD_TESTS    "Build the test harness"        ON)
option(HTMLUI_BUILD_EXAMPLES "Build the example applications" ON)
option(HTMLUI_ENABLE_SDL3    "Enable the SDL3 renderer"      OFF)
option(HTMLUI_ENABLE_VULKAN  "Enable the Vulkan renderer"    OFF)

set(HTMLUI_SOURCES
    # Shared types — __html_node__, CssRule, DisplayList, ComputedStyle
    src/internal/types.c

    # Parser
    src/parser/html.c
    src/parser/css.c

    # Style resolution
    src/style/cascade.c

    # Layout engine
    src/layout/layout.c

    # Display list builder
    src/paint/paint.c

    # Renderer — software path always included
    src/render/soft.c
    # src/render/vulkan.c (enabled below with HTMLUI_ENABLE_VULKAN)

    # Font rendering                 ← Milestone 4
    # src/font/font.c

    # Event system
    src/events/events.c

    # Public API implementation
    src/api/api.c
)

# Library target
add_library(htmlui STATIC ${HTMLUI_SOURCES})

target_include_directories(htmlui
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link math library + FreeType
target_link_libraries(htmlui PUBLIC m)
find_package(Freetype)
if(FREETYPE_FOUND)
    target_include_directories(htmlui PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(htmlui PUBLIC ${FREETYPE_LIBRARIES})
    target_compile_definitions(htmlui PUBLIC HTMLUI_FREETYPE=1)
    message(STATUS "FreeType: found")
else()
    message(STATUS "FreeType: not found — text will use placeholder bars")
endif()

# SDL3 renderer
if (HTMLUI_ENABLE_SDL3)
    find_package(SDL3 REQUIRED)
    # sdl3.c is only meaningful with SDL3 — add it conditionally
    target_sources(htmlui PRIVATE src/render/sdl3.c)
    target_link_libraries(htmlui PUBLIC SDL3::SDL3)
    target_compile_definitions(htmlui PUBLIC HTMLUI_SDL3=1)
    # SDL3 headers need to be visible to all modules that include sdl3.h
    target_include_directories(htmlui PRIVATE ${SDL3_INCLUDE_DIRS})
    message(STATUS "SDL3 renderer: enabled")
else()
    message(STATUS "SDL3 renderer: disabled (Milestone 4+)")
endif()

# Vulkan renderer  (enabled in Milestone 9)
if (HTMLUI_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(htmlui PUBLIC Vulkan::Vulkan)
    target_compile_definitions(htmlui PUBLIC HTMLUI_VULKAN=1)
    message(STATUS "Vulkan renderer: enabled")
else()
    message(STATUS "Vulkan renderer: disabled (Milestone 9+)")
endif()

# Tests
if (HTMLUI_BUILD_TESTS)
    enable_testing()

    add_executable(test_parser tests/test_parser.c)
    target_link_libraries(test_parser PRIVATE htmlui)
    target_include_directories(test_parser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Run with the bundled example files so the file-based test works
    add_test(
        NAME parser_unit_tests
        COMMAND test_parser
    )

    add_test(
        NAME parser_file_test
        COMMAND test_parser
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css
    )

    add_executable(test_milestone5 tests/test_milestone5.c)
    target_link_libraries(test_milestone5 PRIVATE htmlui)
    target_include_directories(test_milestone5 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_test(
        NAME milestone5_api
        COMMAND test_milestone5
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Add these new blocks for test_render:
    add_executable(test_render tests/test_render.c)
    target_link_libraries(test_render PRIVATE htmlui)
    target_include_directories(test_render PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(
        NAME render_test
        COMMAND test_render
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css
        /tmp/htmlui_render.ppm
    )

    message(STATUS "Tests: enabled")
endif()

# Examples
if (HTMLUI_BUILD_EXAMPLES AND HTMLUI_ENABLE_SDL3)
    add_executable(hello_app examples/hello/main.c)
    target_link_libraries(hello_app PRIVATE htmlui)
    target_include_directories(hello_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # The app loads views/main.html and styles/app.css relative to CWD.
    # Set the working directory so those paths resolve correctly when run
    # from the build directory via ctest or directly.
    set_target_properties(hello_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    message(STATUS "Examples: enabled — run from ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/")
    message(STATUS "  cd examples/hello && ../../build/hello_app")
elseif(HTMLUI_BUILD_EXAMPLES)
    message(STATUS "Examples: skipped (requires HTMLUI_ENABLE_SDL3=ON)")
endif()

# Install rules  (optional — useful when packaging the library)
install(TARGETS htmlui
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(FILES include/htmlui.h DESTINATION include)

message(STATUS "")
message(STATUS "htmlui ${PROJECT_VERSION} configuration summary")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests       : ${HTMLUI_BUILD_TESTS}")
message(STATUS "  SDL3        : ${HTMLUI_ENABLE_SDL3}")
message(STATUS "  Vulkan      : ${HTMLUI_ENABLE_VULKAN}")
message(STATUS "")
