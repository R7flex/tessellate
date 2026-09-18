cmake_minimum_required(VERSION 3.20)
project(LLVMObfuscationx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LT_LLVM_INSTALL_DIR "" CACHE PATH "LLVM install prefix (contains lib/cmake/llvm)")
set(LT_OPT_IMPLIB "" CACHE FILEPATH "Import lib for opt.exe (opt.lib)")

if(LT_LLVM_INSTALL_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${LT_LLVM_INSTALL_DIR}/lib/cmake/llvm")
endif()

find_package(LLVM REQUIRED CONFIG)
if(LLVM_VERSION_MAJOR VERSION_LESS 18)
  message(FATAL_ERROR "Found LLVM ${LLVM_PACKAGE_VERSION}, need 18.x (prefer 18.1.5)")
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})

if(NOT LLVM_ENABLE_RTTI)
  if(MSVC)
    string(REGEX REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
  endif()
endif()

set(TESS_PLUGIN_SOURCES
  src/Plugin.cpp
  src/PluginAnchors.cpp
  src/Utils.cpp
  src/stubs/Flattening.cpp
  src/stubs/BogusCF.cpp
  src/stubs/Substitution.cpp
  src/stubs/SizeTrim.cpp
)

if(WIN32)
  if(NOT LT_OPT_IMPLIB)
    if(EXISTS "${LT_LLVM_INSTALL_DIR}/lib/opt.lib")
      set(LT_OPT_IMPLIB "${LT_LLVM_INSTALL_DIR}/lib/opt.lib")
    endif()
  endif()
  if(NOT LT_OPT_IMPLIB OR NOT EXISTS "${LT_OPT_IMPLIB}")
    message(FATAL_ERROR
      "Windows plugin build needs opt.lib (import library for opt.exe).\n"
      "Pass -DLT_OPT_IMPLIB=... or run scripts/setup-llvm.ps1")
  endif()
  message(STATUS "Linking plugin against opt import lib: ${LT_OPT_IMPLIB}")

  add_library(LLVMObfuscationx MODULE
    ${TESS_PLUGIN_SOURCES}
    LLVMObfuscationx.def
  )
  target_link_libraries(LLVMObfuscationx PRIVATE "${LT_OPT_IMPLIB}")
else()
  add_library(LLVMObfuscationx MODULE ${TESS_PLUGIN_SOURCES})
  llvm_map_components_to_libnames(llvm_libs
    core support transformutils analysis passes)
  target_link_libraries(LLVMObfuscationx PRIVATE ${llvm_libs})
endif()

target_include_directories(LLVMObfuscationx PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(LLVMObfuscationx PRIVATE /utf-8 /W3)
endif()

set_target_properties(LLVMObfuscationx PROPERTIES
  OUTPUT_NAME "LLVMObfuscationx"
  PREFIX ""
)

add_custom_command(TARGET LLVMObfuscationx POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:LLVMObfuscationx>
    ${CMAKE_BINARY_DIR}/LLVMObfuscationx.dll
  COMMENT "Copy LLVMObfuscationx.dll to build root"
)
