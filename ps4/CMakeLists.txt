cmake_minimum_required(VERSION 3.21)
project(cloudmoon_ps4 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(cloudmoon_ps4_ui STATIC
    src/ui/cloudmoon_ui.c
    src/qr/qrcodegen.c
    src/qr/cloudmoon_qr.c
)

# Platform-independent CloudMoon signalling/protocol code reused by the PS4 port.
add_library(cloudmoon_protocol_core STATIC
    ../src/cloudmoon_protocol.c
    ../src/cloudmoon_session.c
    ../src/cloudmoon_discovery.c
    ../src/cloudmoon_ws.c
    ../src/cloudmoon_ws_tls.c
    ../src/cloudmoon_client.c
)
target_include_directories(cloudmoon_protocol_core PUBLIC ../src)
target_compile_options(cloudmoon_protocol_core PRIVATE -Wall -Wextra -Werror)

add_library(cloudmoon_catalog_parse STATIC
    src/catalog/cloudmoon_catalog_parse.c
)
target_include_directories(cloudmoon_catalog_parse PUBLIC src/catalog)
target_compile_options(cloudmoon_catalog_parse PRIVATE -Wall -Wextra -Werror)

add_library(cloudmoon_account_core STATIC
    src/account/cloudmoon_session_link.c
)
target_include_directories(cloudmoon_account_core PUBLIC src/account ../src)
target_link_libraries(cloudmoon_account_core PUBLIC cloudmoon_protocol_core)
target_compile_options(cloudmoon_account_core PRIVATE -Wall -Wextra -Werror)
target_include_directories(cloudmoon_ps4_ui PUBLIC src/ui src/qr)
target_compile_options(cloudmoon_ps4_ui PRIVATE -Wall -Wextra -Werror)


if(PS4)
    # Mbed TLS is pinned as a repository submodule. Build it statically for
    # Orbis; TLS 1.2 verification is performed by cloudmoon_tls_mbedtls.c.
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(GEN_FILES OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    add_compile_definitions(MBEDTLS_NO_PLATFORM_ENTROPY)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/mbedtls/framework/CMakeLists.txt")
        message(FATAL_ERROR "Mbed TLS framework submodule is missing; initialize vendor/mbedtls recursively")
    endif()

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/mbedtls"
                     "${CMAKE_CURRENT_BINARY_DIR}/mbedtls")


    # libdatachannel is the same WebRTC engine used by the portable CloudMoon
    # adapter. Build its static C API, SCTP, ICE and SRTP support for Orbis.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_DEPS_LIBS OFF CACHE BOOL "" FORCE)
    set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
    # libSRTP uses its own optional MbedTLS crypto backend; keep that disabled
    # because libdatachannel only needs SRTP's internal AES/HMAC primitives.
    set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_SRTP OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_JUICE OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_USRSCTP OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_PLOG OFF CACHE BOOL "" FORCE)
    set(USE_SYSTEM_JSON OFF CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_MEDIA OFF CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(RTC_UPDATE_VERSION_HEADER OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libdatachannel/CMakeLists.txt")
        message(FATAL_ERROR "libdatachannel submodule is missing")
    endif()

    if(NOT TARGET MbedTLS::MbedTLS)
        add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
        set_property(TARGET MbedTLS::MbedTLS PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/mbedtls/include")
        set_property(TARGET MbedTLS::MbedTLS PROPERTY
            INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto")
    endif()

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libdatachannel"
                     "${CMAKE_CURRENT_BINARY_DIR}/libdatachannel")

    # usrsctp's FreeBSD userspace code refers to _ALIGN from sys/param.h.
    # OpenOrbis does not ship that BSD kernel header; supply the portable
    # equivalent only to the SCTP target.
    if(TARGET usrsctp)
        target_compile_options(usrsctp PRIVATE
            "-include${CMAKE_CURRENT_SOURCE_DIR}/src/compat/orbis_compat.h")
    endif()

    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/opus/CMakeLists.txt")
        message(FATAL_ERROR "Opus source missing; fetch pinned xiph/opus commit in CI")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../vendor/opus"
                     "${CMAKE_CURRENT_BINARY_DIR}/opus")


    add_library(cloudmoon_tls_mbedtls STATIC
        src/net/cloudmoon_tls_mbedtls.c
        src/net/cloudmoon_random_mbedtls.c
        src/net/cloudmoon_mbedtls_entropy.c
    )
    target_include_directories(cloudmoon_tls_mbedtls PUBLIC
        src/net ../src
    )
    target_link_libraries(cloudmoon_tls_mbedtls PUBLIC
        mbedtls mbedx509 mbedcrypto
    )
    target_compile_options(cloudmoon_tls_mbedtls PRIVATE -Wall -Wextra)

    target_link_libraries(cloudmoon_protocol_core PUBLIC cloudmoon_tls_mbedtls)
endif()

option(CM_UI_BUILD_TESTS "Build host-side dashboard tests" ON)
if(CM_UI_BUILD_TESTS)
    enable_testing()
    add_executable(cloudmoon_ps4_ui_smoke tests/ui_smoke.c)
    target_link_libraries(cloudmoon_ps4_ui_smoke PRIVATE cloudmoon_ps4_ui)
    target_compile_options(cloudmoon_ps4_ui_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_ps4_ui_smoke COMMAND cloudmoon_ps4_ui_smoke)

    add_executable(cloudmoon_protocol_smoke tests/protocol_smoke.c)
    target_link_libraries(cloudmoon_protocol_smoke PRIVATE cloudmoon_protocol_core)
    target_compile_options(cloudmoon_protocol_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_protocol_smoke COMMAND cloudmoon_protocol_smoke)

    add_executable(cloudmoon_qr_smoke tests/qr_smoke.c)
    target_link_libraries(cloudmoon_qr_smoke PRIVATE cloudmoon_ps4_ui)
    target_compile_options(cloudmoon_qr_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_qr_smoke COMMAND cloudmoon_qr_smoke)

    add_executable(cloudmoon_catalog_smoke tests/catalog_smoke.c)
    target_link_libraries(cloudmoon_catalog_smoke PRIVATE cloudmoon_catalog_parse)
    target_compile_options(cloudmoon_catalog_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_catalog_smoke COMMAND cloudmoon_catalog_smoke)

    add_executable(cloudmoon_session_link_smoke tests/session_link_smoke.c)
    target_link_libraries(cloudmoon_session_link_smoke PRIVATE cloudmoon_account_core)
    target_compile_options(cloudmoon_session_link_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_session_link_smoke COMMAND cloudmoon_session_link_smoke)
endif()

if(PS4)
    add_library(cloudmoon_ps4_orbis_platform STATIC
        src/platform/orbis_input.c
        src/platform/orbis_video.c
        src/platform/orbis_h264.c
        src/platform/orbis_audio.c
        src/platform/cloudmoon_orbis_webrtc.c
        src/catalog/cloudmoon_catalog_fetch.c
        src/platform/orbis_dashboard.c
        src/platform/orbis_network.c
        src/net/cloudmoon_orbis_libc_compat.c
        src/account/cloudmoon_pairing.c
        src/account/cloudmoon_orbis_connection.c
        src/account/cloudmoon_orbis_game.c
    )
    target_include_directories(cloudmoon_ps4_orbis_platform PUBLIC
        src/platform src/catalog src/account src/net src/qr
    )
    target_include_directories(cloudmoon_ps4_orbis_platform PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/libdatachannel/include"
    )
    target_link_libraries(cloudmoon_ps4_orbis_platform PUBLIC
        cloudmoon_ps4_ui cloudmoon_protocol_core cloudmoon_account_core
        cloudmoon_catalog_parse
        datachannel-static
        opus
        SceNet
        SceNetCtl
        ScePad
        SceAudioOut
        SceVideoOut
        SceUserService
    )
    target_compile_options(cloudmoon_ps4_orbis_platform PRIVATE -Wall -Wextra)
    # Link the standalone CloudMoon dashboard, pairing and game-streaming app.
    add_executable(cloudmoon_ps4_app src/main.c)
    target_link_libraries(cloudmoon_ps4_app PRIVATE
        cloudmoon_ps4_orbis_platform
        SceSysmodule
        SceGnmDriver
    )
    target_compile_options(cloudmoon_ps4_app PRIVATE -Wall -Wextra)
    set_target_properties(cloudmoon_ps4_app PROPERTIES
        OUTPUT_NAME "cloudmoon_ps4_app"
        SUFFIX ".elf"
    )
endif()
