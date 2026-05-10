cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
include(FetchContent)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(BUILD_TESTING "Build unit tests for the worker" ON)
option(WORKER_ONLY "Build only the worker and tests (no OBS/Qt required)" OFF)

if(NOT WORKER_ONLY)
  include(compilerconfig)
  include(defaults)
  include(helpers)

  # ── Plugin shared library ──────────────────────────────────────────────────

  add_library(${CMAKE_PROJECT_NAME} MODULE)

  find_package(libobs REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

  if(ENABLE_FRONTEND_API)
    find_package(obs-frontend-api REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  endif()

  if(ENABLE_QT)
    find_package(Qt6 COMPONENTS Widgets Core Network REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
    target_compile_options(
      ${CMAKE_PROJECT_NAME}
      PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
    )
    set_target_properties(
      ${CMAKE_PROJECT_NAME}
      PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
    )

    # qtkeychain — cross-platform secure token storage
    FetchContent_Declare(
      qtkeychain
      GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
      GIT_TAG        0.14.3
    )
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(QTKEYCHAIN_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(qtkeychain)
    # qtkeychain sets no BUILD_INTERFACE include dirs, so add them manually:
    # source root for keychain.h, binary dir for the generated qkeychain_export.h
    FetchContent_GetProperties(qtkeychain SOURCE_DIR _qtkc_src BINARY_DIR _qtkc_bin)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${_qtkc_src}" "${_qtkc_bin}")
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE qt6keychain)
  endif()

  # ── OAuth credentials (injected at compile time; not secret) ─────────────────
  if(NOT DEFINED RIZZYTOS_CLIENT_ID OR NOT DEFINED RIZZYTOS_CLIENT_SECRET)
    message(WARNING
      "RIZZYTOS_CLIENT_ID / RIZZYTOS_CLIENT_SECRET not set. "
      "YouTube upload will not work. Pass -DRIZZYTOS_CLIENT_ID=... "
      "-DRIZZYTOS_CLIENT_SECRET=... to CMake.")
    set(RIZZYTOS_CLIENT_ID     "" CACHE STRING "Google OAuth client ID")
    set(RIZZYTOS_CLIENT_SECRET "" CACHE STRING "Google OAuth client secret")
  endif()
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    RIZZYTOS_CLIENT_ID="${RIZZYTOS_CLIENT_ID}"
    RIZZYTOS_CLIENT_SECRET="${RIZZYTOS_CLIENT_SECRET}"
  )

  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/plugin-main.cpp
      src/plugin-settings.cpp
      src/plugin-recorder.cpp
      src/plugin-launcher.cpp
      src/plugin-ui.cpp
      src/youtube-auth.cpp
      src/youtube-uploader.cpp
  )

  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

  set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
endif()

# ── Worker standalone executable ─────────────────────────────────────────────

add_executable(rizzytos-worker
  worker/main.cpp
  worker/args.cpp
  worker/concat.cpp
  worker/progress.cpp
)

target_include_directories(rizzytos-worker PRIVATE worker)
set_property(TARGET rizzytos-worker PROPERTY CXX_STANDARD 17)

if(NOT WORKER_ONLY)
  install(TARGETS rizzytos-worker
    RUNTIME DESTINATION "data/obs-plugins/${CMAKE_PROJECT_NAME}"
  )
endif()

# ── Worker unit tests ─────────────────────────────────────────────────────────

if(BUILD_TESTING)
  enable_testing()

  add_executable(test_worker_args
    tests/worker/test_args.cpp
    worker/args.cpp
  )
  target_include_directories(test_worker_args PRIVATE worker)
  set_property(TARGET test_worker_args PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_args COMMAND test_worker_args)

  add_executable(test_worker_progress
    tests/worker/test_progress.cpp
    worker/progress.cpp
  )
  target_include_directories(test_worker_progress PRIVATE worker)
  set_property(TARGET test_worker_progress PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_progress COMMAND test_worker_progress)

  add_executable(test_worker_concat
    tests/worker/test_concat.cpp
    worker/concat.cpp
    worker/progress.cpp
  )
  target_include_directories(test_worker_concat PRIVATE worker)
  set_property(TARGET test_worker_concat PROPERTY CXX_STANDARD 17)
  add_test(NAME worker_concat COMMAND test_worker_concat)
endif()
