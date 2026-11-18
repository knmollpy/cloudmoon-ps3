cmake_minimum_required(VERSION 3.16)
project(cloudmoon_ps4_ui C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(cloudmoon_ps4_ui STATIC
    src/ui/cloudmoon_ui.c
)
target_include_directories(cloudmoon_ps4_ui PUBLIC src/ui)
target_compile_options(cloudmoon_ps4_ui PRIVATE -Wall -Wextra -Werror)

option(CM_UI_BUILD_TESTS "Build host-side dashboard tests" ON)
if(CM_UI_BUILD_TESTS)
    enable_testing()
    add_executable(cloudmoon_ps4_ui_smoke tests/ui_smoke.c)
    target_link_libraries(cloudmoon_ps4_ui_smoke PRIVATE cloudmoon_ps4_ui)
    target_compile_options(cloudmoon_ps4_ui_smoke PRIVATE -Wall -Wextra -Werror)
    add_test(NAME cloudmoon_ps4_ui_smoke COMMAND cloudmoon_ps4_ui_smoke)
endif()
