# Windows OpenXR API layer exposing Frame passthrough through the standard
# OpenXR paths, built by default (FRAME_OPENXR_LAYER). The setup installs it
# with the PC provider and registers it as an implicit layer; this file only
# builds it. Without OpenXR-SDK headers the layer and its tests are skipped.
set(SFNP_OPENXR_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
find_path(SFNP_OPENXR_HEADERS openxr/openxr.h DOC "OpenXR-SDK include directory")
if(NOT SFNP_OPENXR_HEADERS)
    message(STATUS "OpenXR layer skipped: set SFNP_OPENXR_HEADERS to an OpenXR-SDK include directory")
    return()
endif()
set(SFNP_OPENXR_WARNINGS -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
# Quote-only on GCC/Clang: the repository's VERSION file must not shadow <version>
# on a case-insensitive Windows file system.
function(sfnp_openxr_root target)
    if(MSVC)
        target_include_directories(${target} PRIVATE ${SFNP_OPENXR_ROOT})
    else()
        target_compile_options(${target} PRIVATE -iquote ${SFNP_OPENXR_ROOT})
    endif()
endfunction()

add_executable(test_openxr_layer_policy ${SFNP_OPENXR_ROOT}/tests/test_openxr_layer_policy.cpp)
sfnp_openxr_root(test_openxr_layer_policy)
target_include_directories(test_openxr_layer_policy SYSTEM PRIVATE ${SFNP_OPENXR_HEADERS})
if(NOT MSVC)
    target_compile_options(test_openxr_layer_policy PRIVATE ${SFNP_OPENXR_WARNINGS})
endif()
if(MINGW)
    target_link_options(test_openxr_layer_policy PRIVATE -static)
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_test(NAME openxr_layer_policy COMMAND test_openxr_layer_policy)
endif()

if(WIN32)
    add_library(XrApiLayer_sfnp_passthrough SHARED
        ${SFNP_OPENXR_ROOT}/openxr/openxr_layer.cpp ${SFNP_OPENXR_ROOT}/openxr/openxr_source_windows.cpp)
    set_target_properties(XrApiLayer_sfnp_passthrough PROPERTIES PREFIX "")
    sfnp_openxr_root(XrApiLayer_sfnp_passthrough)
    target_include_directories(XrApiLayer_sfnp_passthrough SYSTEM PRIVATE ${SFNP_OPENXR_HEADERS})
    target_compile_definitions(XrApiLayer_sfnp_passthrough PRIVATE XR_USE_PLATFORM_WIN32)
    target_link_libraries(XrApiLayer_sfnp_passthrough PRIVATE bcrypt)
    if(NOT MSVC)
        target_compile_options(XrApiLayer_sfnp_passthrough PRIVATE ${SFNP_OPENXR_WARNINGS})
    endif()
    if(MINGW)
        target_link_options(XrApiLayer_sfnp_passthrough PRIVATE -static) # No libc++/libgcc DLL next to the game.
    endif()
    add_custom_command(TARGET XrApiLayer_sfnp_passthrough POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy
        ${SFNP_OPENXR_ROOT}/openxr/XrApiLayer_sfnp_passthrough.json $<TARGET_FILE_DIR:XrApiLayer_sfnp_passthrough>)

    # The client against the provider's own source server on a real pipe.
    add_executable(test_openxr_source_pipe_windows ${SFNP_OPENXR_ROOT}/tests/test_openxr_source_pipe_windows.cpp
        ${SFNP_OPENXR_ROOT}/openxr/openxr_source_windows.cpp ${SFNP_OPENXR_ROOT}/pc/stream-bridge-sender-provider-sourceipc.cpp)
    sfnp_openxr_root(test_openxr_source_pipe_windows)
    target_link_libraries(test_openxr_source_pipe_windows PRIVATE bcrypt advapi32)
    if(NOT MSVC)
        target_compile_options(test_openxr_source_pipe_windows PRIVATE ${SFNP_OPENXR_WARNINGS})
    endif()
    if(MINGW)
        target_link_options(test_openxr_source_pipe_windows PRIVATE -static) # No libc++/libgcc DLL next to the game.
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME openxr_source_pipe_windows COMMAND test_openxr_source_pipe_windows)
    endif()
endif()
