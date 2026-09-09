# Locates teez-worker when embedding the worker into teez-cli.
if(TARGET teez_worker_lib)
    return()
endif()

include(FetchContent)

set(TEEZ_EMBED_WORKER_DIR "" CACHE PATH "Path to teez-worker source tree")
set(TEEZ_WORKER_GIT_REPOSITORY "https://github.com/Coditary/teez-worker.git" CACHE STRING
    "Git repository used when teez-worker is fetched automatically")
set(TEEZ_WORKER_GIT_TAG "main" CACHE STRING
    "Git tag or branch used when teez-worker is fetched automatically")
option(TEEZ_FETCH_WORKER_IF_MISSING
    "Fetch teez-worker from GitHub when not found locally" ON)

if(NOT TEEZ_EMBED_WORKER_DIR)
    set(_default "${CMAKE_CURRENT_LIST_DIR}/../../teez-worker")
    if(EXISTS "${_default}/CMakeLists.txt")
        set(TEEZ_EMBED_WORKER_DIR "${_default}" CACHE PATH "Path to teez-worker source tree" FORCE)
    endif()
endif()

if(TEEZ_EMBED_WORKER_DIR AND EXISTS "${TEEZ_EMBED_WORKER_DIR}/CMakeLists.txt")
    set(TEEZ_WORKER_SOURCE_DIR "${TEEZ_EMBED_WORKER_DIR}")
elseif(TEEZ_FETCH_WORKER_IF_MISSING)
    message(STATUS "teez-worker not found locally; fetching from ${TEEZ_WORKER_GIT_REPOSITORY}")
    FetchContent_Declare(
        teez_worker
        GIT_REPOSITORY "${TEEZ_WORKER_GIT_REPOSITORY}"
        GIT_TAG "${TEEZ_WORKER_GIT_TAG}"
        GIT_SHALLOW TRUE
    )
    FetchContent_GetProperties(teez_worker)
    if(NOT teez_worker_POPULATED)
        FetchContent_Populate(teez_worker)
    endif()
    set(TEEZ_EMBED_WORKER_DIR "${teez_worker_SOURCE_DIR}" CACHE PATH "Path to teez-worker source tree" FORCE)
    set(TEEZ_WORKER_SOURCE_DIR "${teez_worker_SOURCE_DIR}")
else()
    message(STATUS "teez-worker not found; building teez without embedded worker")
endif()
