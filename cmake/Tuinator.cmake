option(TEEZ_ENABLE_UI "Build live terminal UI via Tuinator (requires ncursesw)" ON)

if(NOT TEEZ_ENABLE_UI)
    return()
endif()

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(TEEZ_NCURSES QUIET ncursesw)
endif()

if(NOT TEEZ_NCURSES_FOUND)
    set(CURSES_NEED_NCURSES TRUE)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses QUIET)
    if(NOT CURSES_FOUND)
        message(WARNING "teez-cli: ncursesw not found; live UI disabled (pass -DTEEZ_ENABLE_UI=OFF)")
        set(TEEZ_ENABLE_UI OFF)
        return()
    endif()
endif()

include(FetchContent)

set(TUINATOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TUINATOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TUINATOR_INSTALL OFF CACHE BOOL "" FORCE)

set(_tuinator_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../Tuinator")
if(EXISTS "${_tuinator_dir}/CMakeLists.txt")
    FetchContent_Declare(
        tuinator
        SOURCE_DIR "${_tuinator_dir}"
    )
else()
    FetchContent_Declare(
        tuinator
        GIT_REPOSITORY https://github.com/Coditary/Tuinator.git
        GIT_TAG main
        GIT_SHALLOW TRUE
    )
endif()

FetchContent_MakeAvailable(tuinator)

if(NOT TARGET tuinator::tuinator)
    message(WARNING "teez-cli: Tuinator target missing; live UI disabled")
    set(TEEZ_ENABLE_UI OFF)
    return()
endif()

function(teez_apply_ui_target target_name)
    target_sources(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/run_output_ui.cpp
    )

    if(NOT TEEZ_ENABLE_UI OR NOT TARGET tuinator::tuinator)
        return()
    endif()

    target_link_libraries(${target_name} PRIVATE tuinator::tuinator)
    target_compile_definitions(${target_name} PRIVATE TEEZ_ENABLE_UI=1)
endfunction()
