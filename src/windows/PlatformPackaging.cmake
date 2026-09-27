# Windows packaging: deployment, installer setup, and post-build steps

# Target Windows 10, in line with Qt requirements
add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -DNTDDI_VERION=0x0A000000)

# Strip debug symbols in release mode
if(CMAKE_STRIP AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_STRIP}" "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Stripping $<TARGET_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM)
endif()

# Optional code signing
if (IMAGER_SIGNED_APP)
    # Determine build architecture
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(arch x64)
    else ()
        set(arch x86)
    endif ()

    # Find signtool from Windows 10 SDK
    if (NOT SIGNTOOL)
        set(win10_kit_versions)
        set(regkey "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows Kits\\Installed Roots")
        set(regval "KitsRoot10")
        get_filename_component(w10_kits_path "[${regkey};${regval}]" ABSOLUTE CACHE)
        if (w10_kits_path)
            message(WARNING "Found Windows 10 kits path: ${w10_kits_path}")
            file(GLOB w10_kit_versions "${w10_kits_path}/bin/10.*")
            list(REVERSE w10_kit_versions)
        endif ()
        unset(w10_kits_path CACHE)
        if (w10_kit_versions)
            find_program(SIGNTOOL
                NAMES signtool
                PATHS ${w10_kit_versions}
                PATH_SUFFIXES ${arch} bin/${arch} bin
                NO_DEFAULT_PATH
            )
        endif ()
    endif ()

    if (NOT SIGNTOOL)
        message(FATAL_ERROR "Unable to locate signtool.exe used for code signing")
    endif()
    add_definitions(-DSIGNTOOL="${SIGNTOOL}")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a
                "$<TARGET_FILE:${PROJECT_NAME}>")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${SIGNTOOL}" sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a
                "${CMAKE_BINARY_DIR}/rpi-imager-callback-relay.exe")

endif()

# Gleem Imager ships no WinUSB driver: rpiboot (Raspberry Pi USB boot mode)
# is not offered, so neither the INF nor its catalog is deployed.

# windeployqt
find_program(WINDEPLOYQT "windeployqt.exe" PATHS "${Qt6_ROOT}/bin")
if (NOT WINDEPLOYQT)
    message(FATAL_ERROR "Unable to locate windeployqt.exe")
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deploy")

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_SOURCE_DIR}/../license.txt"
        "${CMAKE_SOURCE_DIR}/../NOTICE"
        "${CMAKE_SOURCE_DIR}/windows/gleem-imager-cli.cmd"
        "${CMAKE_BINARY_DIR}/rpi-imager-callback-relay.exe"
        "${CMAKE_BINARY_DIR}/deploy")

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND "${WINDEPLOYQT}" --no-translations --no-widgets --skip-plugin-types qmltooling --exclude-plugins qtiff,qwebp,qgif --no-quickcontrols2fusion --no-quickcontrols2fusionstyleimpl --no-quickcontrols2universal --no-quickcontrols2universalstyleimpl --no-quickcontrols2imagine --no-quickcontrols2imaginestyleimpl --no-quickcontrols2fluentwinui3styleimpl --no-quickcontrols2windowsstyleimpl --verbose 2 --qmldir "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/deploy/$<TARGET_FILE_NAME:${PROJECT_NAME}>")

# NSIS or Inno Setup configuration
option(ENABLE_INNO_INSTALLER "Build Inno Setup installer instead of NSIS" OFF)

# Extra (non-version) variables needed by installer templates — written once
# at configure time, combined with version vars at build time.
set(_installer_extra_vars "${CMAKE_CURRENT_BINARY_DIR}/installer_extra_vars.cmake")
file(WRITE "${_installer_extra_vars}"
    "set(CMAKE_BINARY_DIR \"${CMAKE_BINARY_DIR}\")\n"
    "set(CMAKE_SOURCE_DIR \"${CMAKE_SOURCE_DIR}\")\n"
)

if(ENABLE_INNO_INSTALLER)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/installer")

    # Configure .iss at build time so the version matches the binary
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss"
        COMMAND ${CMAKE_COMMAND}
            -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
            -DEXTRA_VARS_FILE=${_installer_extra_vars}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.iss.in
            -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss
            -P ${CONFIGURE_VERSIONED_SCRIPT}
        DEPENDS
            ${IMAGER_VERSION_VARS}
            ${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.iss.in
        COMMENT "Configuring rpi-imager.iss with build-time version"
        VERBATIM
    )

    find_program(INNO_COMPILER NAMES iscc ISCC "iscc.exe" PATHS
        "C:/Program Files (x86)/Inno Setup 6"
        "C:/Program Files/Inno Setup 6"
        DOC "Path to Inno Setup compiler")

    if(INNO_COMPILER)
        if(IMAGER_SIGNED_APP)
            add_custom_target(inno_installer
                COMMAND "${INNO_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss" "/DSIGNING_ENABLED" "/Ssign=${SIGNTOOL} sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /a $p"
                DEPENDS ${PROJECT_NAME} "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss"
                COMMENT "Building Inno Setup installer"
                VERBATIM)
        else()
            add_custom_target(inno_installer
                COMMAND "${INNO_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss"
                DEPENDS ${PROJECT_NAME} "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.iss"
                COMMENT "Building Inno Setup installer"
                VERBATIM)
        endif()
        message(STATUS "Added 'inno_installer' target to build the Inno Setup installer")
    else()
        message(WARNING "Inno Setup compiler not found. Install Inno Setup from https://jrsoftware.org/isinfo.php")
    endif()
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.nsi.in")
        # Configure .nsi at build time so the version matches the binary
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.nsi"
            COMMAND ${CMAKE_COMMAND}
                -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
                -DEXTRA_VARS_FILE=${_installer_extra_vars}
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.nsi.in
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.nsi
                -P ${CONFIGURE_VERSIONED_SCRIPT}
            DEPENDS
                ${IMAGER_VERSION_VARS}
                ${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.nsi.in
            COMMENT "Configuring rpi-imager.nsi with build-time version"
            VERBATIM
        )
    else()
        message(STATUS "NSIS template not found; skipping NSIS installer configuration")
    endif()
endif()

# Resource file and manifest: generated at build time so version stays
# in sync with the binary even without a re-configure.
# Manifest must be generated before the .rc (which embeds it via RT_MANIFEST)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.manifest"
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.manifest.in
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.manifest
        -P ${CONFIGURE_VERSIONED_SCRIPT}
    DEPENDS
        ${IMAGER_VERSION_VARS}
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.manifest.in
    COMMENT "Configuring rpi-imager.manifest with build-time version"
    VERBATIM
)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.rc"
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_VARS_FILE=${IMAGER_VERSION_VARS}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.rc.in
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.rc
        -P ${CONFIGURE_VERSIONED_SCRIPT}
    DEPENDS
        ${IMAGER_VERSION_VARS}
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.manifest"
    COMMENT "Configuring rpi-imager.rc with build-time version"
    VERBATIM
)

add_custom_command(TARGET ${PROJECT_NAME}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.manifest"
        "${CMAKE_CURRENT_SOURCE_DIR}/windows/rpi-imager.manifest"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rpi-imager.manifest"
    COMMENT "Copying generated manifest for resource compilation")

add_custom_command(TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${MINGW64_ROOT}/bin/libgcc_s_seh-1.dll"
        "${MINGW64_ROOT}/bin/libstdc++-6.dll"
        "${MINGW64_ROOT}/bin/libwinpthread-1.dll"
        "${CMAKE_BINARY_DIR}/deploy")


