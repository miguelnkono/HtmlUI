cmake_minimum_required(VERSION 3.20)
project(htmlui VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler: ${CMAKE_C_COMPILER}")

option(HTMLUI_BUILD_TESTS    "Build the test harness"         ON)
option(HTMLUI_BUILD_EXAMPLES "Build the example applications" ON)
option(HTMLUI_ENABLE_SDL3    "Enable the SDL3 renderer"       OFF)
option(HTMLUI_ENABLE_VULKAN  "Enable the Vulkan renderer"     OFF)

set(HTMLUI_SOURCES
    src/internal/types.c
    src/parser/html.c
    src/parser/css.c
    src/style/cascade.c
    src/layout/layout.c
    src/paint/paint.c
    src/render/soft.c
    src/events/events.c
    src/api/api.c
)

add_library(htmlui STATIC ${HTMLUI_SOURCES})

target_include_directories(htmlui
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

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

if(HTMLUI_ENABLE_SDL3)
    find_package(SDL3 REQUIRED)
    target_sources(htmlui PRIVATE src/render/sdl3.c)
    target_link_libraries(htmlui PUBLIC SDL3::SDL3)
    target_compile_definitions(htmlui PUBLIC HTMLUI_SDL3=1)
    target_include_directories(htmlui PRIVATE ${SDL3_INCLUDE_DIRS})
    message(STATUS "SDL3 renderer: enabled")
else()
    message(STATUS "SDL3 renderer: disabled (Milestone 4+)")
endif()

if(HTMLUI_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(htmlui PUBLIC Vulkan::Vulkan)
    target_compile_definitions(htmlui PUBLIC HTMLUI_VULKAN=1)
    message(STATUS "Vulkan renderer: enabled")
else()
    message(STATUS "Vulkan renderer: disabled (Milestone 9+)")
endif()

# Tests 
if(HTMLUI_BUILD_TESTS)
    enable_testing()

    # Helper macro so each test target is defined consistently
    macro(add_htmlui_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE htmlui)
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
    endmacro()

    # Milestone 1 — Parser
    add_htmlui_test(test_parser tests/test_parser.c)
    add_test(NAME parser_unit_tests  COMMAND test_parser)
    add_test(NAME parser_file_test   COMMAND test_parser
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css)

    # Milestone 2 — Cascade  (FIX: was missing from original CMakeLists.txt)
    add_htmlui_test(test_cascade tests/test_cascade.c)
    add_test(NAME cascade_unit_tests COMMAND test_cascade)
    add_test(NAME cascade_file_test  COMMAND test_cascade
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css)

    # Milestone 3 — Layout  (FIX: was missing from original CMakeLists.txt)
    add_htmlui_test(test_layout tests/test_layout.c)
    add_test(NAME layout_unit_tests COMMAND test_layout)
    add_test(NAME layout_file_test  COMMAND test_layout
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css)

    # Milestone 4 — Display list + software renderer
    add_htmlui_test(test_render tests/test_render.c)
    add_test(NAME render_test COMMAND test_render
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/views/main.html
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello/styles/app.css
        /tmp/htmlui_render.ppm)

    # Milestone 5 — Full public API
    add_htmlui_test(test_milestone5 tests/test_milestone5.c)
    add_test(NAME milestone5_api COMMAND test_milestone5
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    message(STATUS "Tests: enabled (5 suites)")
endif()

# Examples 
if(HTMLUI_BUILD_EXAMPLES AND HTMLUI_ENABLE_SDL3)
    add_executable(hello_app examples/hello/main.c)
    target_link_libraries(hello_app PRIVATE htmlui)
    target_include_directories(hello_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(calculator_app examples/calculator/main.c)
    target_link_libraries(calculator_app PRIVATE htmlui)
    target_include_directories(calculator_app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

    set_target_properties(hello_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

    message(STATUS "Examples: enabled")
elseif(HTMLUI_BUILD_EXAMPLES)
    message(STATUS "Examples: skipped (requires HTMLUI_ENABLE_SDL3=ON)")
endif()

# Install 
install(TARGETS htmlui ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(FILES include/htmlui.h DESTINATION include)

message(STATUS "")
message(STATUS "htmlui ${PROJECT_VERSION} configuration summary")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests      : ${HTMLUI_BUILD_TESTS}")
message(STATUS "  SDL3       : ${HTMLUI_ENABLE_SDL3}")
message(STATUS "  Vulkan     : ${HTMLUI_ENABLE_VULKAN}")
message(STATUS "")
