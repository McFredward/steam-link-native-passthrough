# Pristine upstream amalgamation, compiled as C with private symbols.
enable_language(C)
add_library(sfnp_zstd OBJECT third_party/zstd/zstd.c)
set_target_properties(sfnp_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_compile_definitions(sfnp_zstd PRIVATE ZSTD_DISABLE_ASM ZSTDLIB_VISIBILITY=)
if(MSVC)
    target_compile_options(sfnp_zstd PRIVATE /w)
else()
    target_compile_options(sfnp_zstd PRIVATE -w)
endif()

# These portable modules are deliberately not linked into the installed loader,
# receiver, native renderer or a Steam process. Their explicit trust ports are
# not proof of a live provider, authenticated peer or safe image composition.
add_library(sfnp_stream_bridge STATIC
    bridge/stream_bridge_protocol.cpp
    bridge/stream_bridge_auth.cpp
    bridge/stream_bridge_capacity.cpp
    bridge/stream_bridge_frame.cpp
    bridge/stream_bridge_hevc_coverage.cpp
    bridge/stream_bridge_compression.cpp
    bridge/hevc_bridge_sei.cpp
    pc/stream-bridge-sender.cpp
    receiver/stream_bridge_receiver.cpp)
target_include_directories(sfnp_stream_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(sfnp_stream_bridge PRIVATE $<TARGET_OBJECTS:sfnp_zstd>)
if(MSVC)
    target_compile_options(sfnp_stream_bridge PRIVATE /W4 /WX)
else()
    target_compile_options(sfnp_stream_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(test_stream_bridge_mask_config tests/test_stream_bridge_mask_config.cpp)
    target_include_directories(test_stream_bridge_mask_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME stream_bridge_mask_config COMMAND test_stream_bridge_mask_config)
    add_executable(test_stream_bridge_hevc_coverage tests/test_stream_bridge_hevc_coverage.cpp)
    target_link_libraries(test_stream_bridge_hevc_coverage PRIVATE sfnp_stream_bridge)
    add_test(NAME stream_bridge_hevc_coverage COMMAND test_stream_bridge_hevc_coverage)
    add_executable(test_stream_bridge_logging tests/stream_bridge_logging_test.cpp)
    target_include_directories(test_stream_bridge_logging PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME stream_bridge_logging COMMAND test_stream_bridge_logging)
endif()

# Cold explicit preparation and bounded control I/O; not installed or autostarted.
find_package(Threads REQUIRED)
add_library(sfnp_stream_bridge_control STATIC
    bridge/stream_bridge_credentials.cpp bridge/stream_bridge_control.cpp)
target_link_libraries(sfnp_stream_bridge_control PUBLIC sfnp_stream_bridge Threads::Threads)
if(WIN32)
    target_link_libraries(sfnp_stream_bridge_control PRIVATE ws2_32 advapi32 bcrypt)
endif()
if(MSVC)
    target_compile_options(sfnp_stream_bridge_control PRIVATE /W4 /WX)
else()
    target_compile_options(sfnp_stream_bridge_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(SFNP_REPORT_VULKAN_HEADERS vulkan/vulkan.h)
    if(SFNP_REPORT_VULKAN_HEADERS)
        add_executable(test_stream_bridge_renderer_report tests/test_stream_bridge_renderer_report.cpp)
        target_include_directories(test_stream_bridge_renderer_report PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR} ${SFNP_REPORT_VULKAN_HEADERS})
        target_compile_options(test_stream_bridge_renderer_report PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME stream_bridge_renderer_report COMMAND test_stream_bridge_renderer_report)
        endif()
    endif()
    add_executable(test_stream_bridge_local
        tests/stream_bridge_local_test.cpp receiver/stream_bridge_local.cpp receiver/stream_bridge_native_decode.cpp)
    target_link_libraries(test_stream_bridge_local PRIVATE sfnp_stream_bridge Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(test_stream_bridge_local PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME stream_bridge_local COMMAND test_stream_bridge_local)
    endif()

    add_executable(test_stream_bridge_control tests/test_stream_bridge_control.cpp)
    target_link_libraries(test_stream_bridge_control PRIVATE sfnp_stream_bridge_control)
    target_compile_options(test_stream_bridge_control PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME stream_bridge_control COMMAND test_stream_bridge_control)
    endif()

    add_executable(test_stream_bridge_receiver_lifetime
        tests/stream-bridge-receiver-lifetime/fixture.cpp receiver/stream_bridge_native_runtime.cpp)
    target_link_libraries(test_stream_bridge_receiver_lifetime PRIVATE sfnp_stream_bridge_control)
    target_compile_options(test_stream_bridge_receiver_lifetime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME stream_bridge_receiver_lifetime COMMAND test_stream_bridge_receiver_lifetime)
    endif()
    add_executable(test_stream_bridge_native_runtime
        tests/stream_bridge_native_runtime_test.cpp
        receiver/stream_bridge_native_runtime.cpp receiver/stream_bridge_native_decode.cpp
        receiver/stream_bridge_native_loader.cpp)
    target_link_libraries(test_stream_bridge_native_runtime PRIVATE sfnp_stream_bridge_control ${CMAKE_DL_LIBS})
    target_compile_options(test_stream_bridge_native_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        enable_language(ASM)
        target_sources(test_stream_bridge_native_runtime PRIVATE receiver/stream_bridge_native_decode_entry.S)
        target_sources(test_stream_bridge_local PRIVATE receiver/stream_bridge_native_decode_entry.S)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME stream_bridge_native_runtime COMMAND test_stream_bridge_native_runtime)
    endif()

    # Explicit SDK input: configure/build never downloads a header. CI prepares
    # the exact reviewed public SDK before enabling actual sender-boundary tests.
    set(SFNP_NVENC_SDK_DIRECTORY "" CACHE PATH "Explicit reviewed NVENC SDK11.1 include directory")
    if(SFNP_NVENC_SDK_DIRECTORY)
        file(SHA256 "${SFNP_NVENC_SDK_DIRECTORY}/nvEncodeAPI.h" SFNP_NVENC_HEADER_HASH)
        if(NOT SFNP_NVENC_HEADER_HASH STREQUAL "be0c29a41ae3260fd016a9001fd959e747bac93ad21411759736abba4ffdea86")
            message(FATAL_ERROR "The explicit NVENC SDK header differs from the reviewed11.1 pin")
        endif()
        # The real encoder state machine with explicit fake OS/NVENC ports;
        # this never opens a GPU or proves a hardware-generated bitstream.
        add_executable(test_stream_bridge_mask_hevc_encoder
            tests/test_stream_bridge_mask_hevc_encoder.cpp pc/stream-bridge-mask-hevc.cpp)
        target_compile_definitions(test_stream_bridge_mask_hevc_encoder PRIVATE SFNP_MASK_HEVC_HOST_TEST=1)
        target_include_directories(test_stream_bridge_mask_hevc_encoder SYSTEM PRIVATE ${SFNP_NVENC_SDK_DIRECTORY})
        target_compile_options(test_stream_bridge_mask_hevc_encoder PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_libraries(test_stream_bridge_mask_hevc_encoder PRIVATE sfnp_stream_bridge Threads::Threads)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME stream_bridge_mask_hevc_encoder COMMAND test_stream_bridge_mask_hevc_encoder)
        endif()

        add_library(sfnp_stream_sender_boundary STATIC
            pc/stream-bridge-sender-provider-core.cpp pc/stream-bridge-sender-provider-runtime.cpp)
        target_include_directories(sfnp_stream_sender_boundary SYSTEM PUBLIC ${SFNP_NVENC_SDK_DIRECTORY})
        target_compile_options(sfnp_stream_sender_boundary PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_libraries(sfnp_stream_sender_boundary PUBLIC sfnp_stream_bridge_control)
        add_executable(test_stream_bridge_sender_coverage_gates tests/test_stream_bridge_sender_coverage_gates.cpp)
        target_compile_options(test_stream_bridge_sender_coverage_gates PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_libraries(test_stream_bridge_sender_coverage_gates PRIVATE sfnp_stream_sender_boundary)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME stream_bridge_sender_coverage_gates COMMAND test_stream_bridge_sender_coverage_gates)
        endif()
        foreach(suite stream_bridge_sender_provider stream_bridge_sender_provider_input stream_bridge_sender_provider_runtime stream_bridge_sender_stage_pipeline)
            add_executable(test_${suite} tests/test_${suite}.cpp)
            target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_link_libraries(test_${suite} PRIVATE sfnp_stream_sender_boundary)
            if(NOT CMAKE_CROSSCOMPILING)
                add_test(NAME ${suite} COMMAND test_${suite})
            endif()
        endforeach()
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME stream_bridge_sender_provider_input_legacy COMMAND test_stream_bridge_sender_provider_input --legacy-codec)
        endif()
    endif()
endif()

add_executable(stream_bridge_au EXCLUDE_FROM_ALL tools/stream_bridge_au.cpp)
target_link_libraries(stream_bridge_au PRIVATE sfnp_stream_bridge)
if(NOT MSVC)
    target_compile_options(stream_bridge_au PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(test_native_image_contract tests/native_image_contract_test.cpp)
    if(NOT MSVC)
        target_compile_options(test_native_image_contract PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME native_image_contract COMMAND test_native_image_contract)
    endif()
    foreach(suite stream_bridge_protocol stream_bridge_auth stream_bridge_capacity stream_bridge_authenticated stream_bridge_frame stream_bridge_compression stream_bridge_entropy stream_bridge_decode_cost hevc_bridge_sei stream_bridge_sender stream_bridge_receiver stream_bridge_source_contract stream_bridge_sender_provider_sourceipc)
        add_executable(test_${suite} tests/test_${suite}.cpp)
        target_link_libraries(test_${suite} PRIVATE sfnp_stream_bridge)
        if(NOT MSVC)
            target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME ${suite} COMMAND test_${suite})
        endif()
    endforeach()
    if(NOT CMAKE_CROSSCOMPILING)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME native_shader_contract COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_shader_contract.py)
        add_test(NAME steam_link_setup COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_steam_link_setup.py)
        add_test(NAME setup_bootstrap COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_bootstrap.py)
        add_test(NAME setup_ui COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_ui.py)
        add_test(NAME setup_dependencies COMMAND ${Python3_EXECUTABLE} -B
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_setup_dependencies.py)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
        find_package(Threads REQUIRED)
        add_executable(test_stream_bridge_native_decode
            receiver/stream_bridge_native_decode.cpp tests/stream_bridge_native_decode_test.cpp)
        target_compile_options(test_stream_bridge_native_decode PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_libraries(test_stream_bridge_native_decode PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
            enable_language(ASM)
            target_sources(test_stream_bridge_native_decode PRIVATE receiver/stream_bridge_native_decode_entry.S)
            add_executable(test_stream_bridge_native_decode_entry
                tests/stream_bridge_native_decode_test.cpp receiver/stream_bridge_native_decode_entry.S)
            target_compile_definitions(test_stream_bridge_native_decode_entry PRIVATE SFNP_NATIVE_DECODE_ENTRY_TEST=1)
            target_compile_options(test_stream_bridge_native_decode_entry PRIVATE -Wall -Wextra -Wpedantic -Werror)
            if(NOT CMAKE_CROSSCOMPILING)
                add_test(NAME stream_bridge_native_decode_entry COMMAND test_stream_bridge_native_decode_entry)
            endif()
        endif()
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME stream_bridge_native_decode COMMAND test_stream_bridge_native_decode)
            add_test(NAME stream_bridge_native_decode_adaptive COMMAND test_stream_bridge_native_decode --adaptive)
        endif()
    endif()
endif()

if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_executable(test_native_stream_coverage_gpu EXCLUDE_FROM_ALL
            tests/native-fragment-stream-coverage-gpu/fixture.cpp)
        target_compile_options(test_native_stream_coverage_gpu PRIVATE
            -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers)
        target_link_libraries(test_native_stream_coverage_gpu PRIVATE Vulkan::Vulkan)
        # Requires private installed shader inputs and active validation.
        # No public fixture can establish an actual selected-video lease.
    endif()
endif()

# Explicit opt-in diagnostics. These unsigned files never enter the receiver.
if(BUILD_TESTING OR WIN32)
add_library(sfnp_mask_capture STATIC pc/stream-bridge-mask-capture.cpp)
target_include_directories(sfnp_mask_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sfnp_mask_capture PUBLIC Threads::Threads)
if(NOT MSVC)
    target_compile_options(sfnp_mask_capture PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
endif()
if(BUILD_TESTING)
    add_executable(test_stream_bridge_mask_capture tests/test_stream_bridge_mask_capture.cpp)
    target_link_libraries(test_stream_bridge_mask_capture PRIVATE sfnp_mask_capture sfnp_stream_bridge)
    if(NOT MSVC)
        target_compile_options(test_stream_bridge_mask_capture PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME stream_bridge_mask_capture COMMAND test_stream_bridge_mask_capture)
    endif()
endif()
if(BUILD_TESTING OR WIN32)
    if(BUILD_TESTING)
        add_executable(check_mask_capture tools/check_mask_capture.cpp)
    else()
        add_executable(check_mask_capture EXCLUDE_FROM_ALL tools/check_mask_capture.cpp)
    endif()
    target_link_libraries(check_mask_capture PRIVATE sfnp_mask_capture sfnp_stream_bridge)
    if(NOT MSVC)
        target_compile_options(check_mask_capture PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME mask_capture_readers COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_capture_masks.py
        $<TARGET_FILE:test_stream_bridge_mask_capture> $<TARGET_FILE:check_mask_capture>)
endif()

# The Frame decoder is a separate private library. No implicit header search,
# downloads, system-library fallback or direct libavcodec/libavutil link.
set(SFNP_MASK_HEVC_INCLUDE_DIRECTORY "" CACHE PATH "Explicit reviewed FFmpeg8 public include directory (codec62/util60)")
set(SFNP_MASK_HEVC_HOST_CODEC_LIBRARY "" CACHE FILEPATH "Explicit host libavcodec62 for real software mask tests")
set(SFNP_MASK_HEVC_HOST_UTILITY_LIBRARY "" CACHE FILEPATH "Explicit host libavutil60 for real software mask tests")
set(SFNP_MASK_HEVC_HOST_FFMPEG_EXECUTABLE "" CACHE FILEPATH "Explicit host ffmpeg with CPU libx265 for synthetic test fixtures")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(sfnp_mask_hevc_decoder STATIC receiver/stream_bridge_mask_hevc.cpp)
    set_target_properties(sfnp_mask_hevc_decoder PROPERTIES
        POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    target_include_directories(sfnp_mask_hevc_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(sfnp_mask_hevc_decoder PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(sfnp_mask_hevc_decoder PUBLIC sfnp_stream_bridge PRIVATE ${CMAKE_DL_LIBS})
    if(SFNP_MASK_HEVC_INCLUDE_DIRECTORY)
        foreach(header libavcodec/avcodec.h libavcodec/version_major.h libavutil/frame.h libavutil/version.h)
            if(NOT EXISTS "${SFNP_MASK_HEVC_INCLUDE_DIRECTORY}/${header}")
                message(FATAL_ERROR "Explicit HEVC decoder headers are incomplete: ${header}")
            endif()
        endforeach()
        # The implementation also statically checks these public ABI majors.
        file(STRINGS "${SFNP_MASK_HEVC_INCLUDE_DIRECTORY}/libavcodec/version_major.h"
            SFNP_MASK_HEVC_CODEC_MAJOR REGEX "^#define[ \t]+LIBAVCODEC_VERSION_MAJOR[ \t]+62([ \t]|$)")
        file(STRINGS "${SFNP_MASK_HEVC_INCLUDE_DIRECTORY}/libavutil/version.h"
            SFNP_MASK_HEVC_UTILITY_MAJOR REGEX "^#define[ \t]+LIBAVUTIL_VERSION_MAJOR[ \t]+60([ \t]|$)")
        if(NOT SFNP_MASK_HEVC_CODEC_MAJOR OR NOT SFNP_MASK_HEVC_UTILITY_MAJOR)
            message(FATAL_ERROR "Explicit HEVC decoder headers must expose the reviewed FFmpeg8 codec62/util60 ABI")
        endif()
        target_include_directories(sfnp_mask_hevc_decoder SYSTEM PRIVATE ${SFNP_MASK_HEVC_INCLUDE_DIRECTORY})
        target_compile_definitions(sfnp_mask_hevc_decoder PRIVATE SFNP_MASK_HEVC_ENABLED=1)
        if(BUILD_TESTING)
            add_executable(test_stream_bridge_mask_hevc tests/test_stream_bridge_mask_hevc.cpp)
            target_link_libraries(test_stream_bridge_mask_hevc PRIVATE sfnp_mask_hevc_decoder)
            target_compile_options(test_stream_bridge_mask_hevc PRIVATE -Wall -Wextra -Wpedantic -Werror)
            # Includes the production Process implementation through its fixture;
            # do not also compile layer/stream_bridge_process.cpp into this target.
            add_executable(test_stream_bridge_mask_hevc_ingress
                tests/mask-hevc/process_ingress.cpp
                receiver/stream_bridge_native_decode.cpp receiver/stream_bridge_native_loader.cpp
                receiver/stream_bridge_native_runtime.cpp receiver/stream_bridge_native_images.cpp
                receiver/stream_bridge_local.cpp receiver/stream_bridge_camera_client.cpp)
            target_include_directories(test_stream_bridge_mask_hevc_ingress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/layer)
            target_compile_options(test_stream_bridge_mask_hevc_ingress PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_link_libraries(test_stream_bridge_mask_hevc_ingress PRIVATE
                sfnp_mask_hevc_decoder sfnp_stream_bridge_control Threads::Threads ${CMAKE_DL_LIBS})
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
                enable_language(ASM)
                target_sources(test_stream_bridge_mask_hevc_ingress PRIVATE receiver/stream_bridge_native_decode_entry.S)
            endif()
        endif()
    endif()
elseif(SFNP_MASK_HEVC_INCLUDE_DIRECTORY)
    message(FATAL_ERROR "The optional HEVC mask decoder requires Linux")
endif()

set(SFNP_MASK_HEVC_HOST_INPUT_COUNT 0)
foreach(input SFNP_MASK_HEVC_HOST_CODEC_LIBRARY SFNP_MASK_HEVC_HOST_UTILITY_LIBRARY SFNP_MASK_HEVC_HOST_FFMPEG_EXECUTABLE)
    if(${input})
        math(EXPR SFNP_MASK_HEVC_HOST_INPUT_COUNT "${SFNP_MASK_HEVC_HOST_INPUT_COUNT}+1")
        if(NOT EXISTS "${${input}}" OR IS_DIRECTORY "${${input}}")
            message(FATAL_ERROR "Explicit HEVC host fixture input is not a file: ${input}")
        endif()
    endif()
endforeach()
if(SFNP_MASK_HEVC_HOST_INPUT_COUNT GREATER 0)
    if(NOT SFNP_MASK_HEVC_HOST_INPUT_COUNT EQUAL 3)
        message(FATAL_ERROR "HEVC host fixtures require explicit codec, utility and ffmpeg paths together")
    endif()
    if(NOT BUILD_TESTING OR CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT SFNP_MASK_HEVC_INCLUDE_DIRECTORY)
        message(FATAL_ERROR "HEVC host fixtures require a native Linux test build and explicit FFmpeg8 headers")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # The runner owns a TemporaryDirectory and never opens NVENC/V4L2 devices.
    add_test(NAME stream_bridge_mask_hevc_real COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/mask-hevc/run.py
        --decoder $<TARGET_FILE:test_stream_bridge_mask_hevc>
        --ingress $<TARGET_FILE:test_stream_bridge_mask_hevc_ingress>
        --ffmpeg ${SFNP_MASK_HEVC_HOST_FFMPEG_EXECUTABLE}
        --codec ${SFNP_MASK_HEVC_HOST_CODEC_LIBRARY}
        --utility ${SFNP_MASK_HEVC_HOST_UTILITY_LIBRARY})
    set_tests_properties(stream_bridge_mask_hevc_real PROPERTIES TIMEOUT 120)
endif()
