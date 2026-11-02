# SPDX-License-Identifier: Apache-2.0
# Bundled QR Code generator library by Project Nayuki (MIT license)
# From: https://github.com/nayuki/QR-Code-generator
#
# Only the C++ implementation (cpp/qrcodegen.cpp + .hpp) is used. The upstream
# repository has no CMake build of its own, so the one translation unit is
# compiled here as a small static library.

set(QRCODEGEN_VERSION "1.8.0")
rpi_imager_fetch_git_or_vendor(qrcodegen
    VENDOR_DIR qrcodegen
    VENDOR_MARKER cpp/qrcodegen.cpp
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v${QRCODEGEN_VERSION}
)
FetchContent_GetProperties(qrcodegen)
if(NOT qrcodegen_POPULATED)
    FetchContent_Populate(qrcodegen)
endif()

add_library(qrcodegen STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(qrcodegen PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)
set_property(TARGET qrcodegen PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(qrcodegen PRIVATE cxx_std_11)

set(QRCODEGEN_INCLUDE_DIR ${qrcodegen_SOURCE_DIR}/cpp)
set(QRCODEGEN_LIBRARIES qrcodegen)
