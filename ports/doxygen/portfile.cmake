set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

if(VCPKG_TARGET_IS_LINUX)
    set(_archive "doxygen-1.18.0.linux.bin.tar.gz")
    set(_sha512 "fd8a26b73dbb29e18ad1966ea44ce09409e23ea6a91b218dcb65d36fb118fcc385ef5b92be93856a54e2746630a2c4c9a4940d0df940e494f918d2a290ed8f96")
elseif(VCPKG_TARGET_IS_WINDOWS)
    set(_archive "doxygen-1.18.0.windows.x64.bin.zip")
    set(_sha512 "c4f7b45a4ae5f49b9d232036ec200033be7a44c41dfa717f121c05a06a9377838795ef9e931267dcb4d73be2d3bb34c97d79f46f464c8a4d62ef4664210e5491")
elseif(VCPKG_TARGET_IS_OSX)
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(_archive "doxygen-1.18.0-mac-arm.zip")
        set(_sha512 "f56f65f46bb8b8e184677997f63ca6fabcc80da3339e28de7249bac5ce851fd91171447037e1750ef952e5192036fc74593eb57d3c0b39a656624105d63ed530")
    else()
        set(_archive "doxygen-1.18.0-mac-intel.zip")
        set(_sha512 "70fb5e7c55681193d9a5f27e684e61187c3af6b793dd2527c20fec23548de1540d0f8a9045f62f161c68853879b3a6233a1d372c5938d505cf3edd035b5f77a1")
    endif()
else()
    message(FATAL_ERROR
        "doxygen overlay port: unsupported triplet '${TARGET_TRIPLET}'. "
        "Supported: x64-linux, x64-windows, x64-osx, arm64-osx.")
endif()

vcpkg_download_distfile(_archive_path
    URLS "https://github.com/doxygen/doxygen/releases/download/Release_1_18_0/${_archive}"
    FILENAME "${_archive}"
    SHA512 "${_sha512}"
)

vcpkg_extract_source_archive(_source_path
    ARCHIVE "${_archive_path}"
    NO_REMOVE_ONE_LEVEL
)

set(_tools_dir "${CURRENT_PACKAGES_DIR}/tools/doxygen")

if(VCPKG_TARGET_IS_WINDOWS)
    set(_tool_files "${_source_path}/doxygen.exe")
    list(APPEND _tool_files
        "${_source_path}/doxywizard.exe"
        "${_source_path}/doxyindexer.exe"
        "${_source_path}/doxysearch.cgi.exe"
        "${_source_path}/libclang.dll"
    )
else()
    if(VCPKG_TARGET_IS_LINUX)
        set(_tool_files "${_source_path}/doxygen-1.18.0/bin/doxygen")
    else()
        set(_tool_files "${_source_path}/doxygen-1.18.0/doxygen")
    endif()
endif()

foreach(_tool IN LISTS _tool_files)
    if(NOT EXISTS "${_tool}")
        message(FATAL_ERROR "doxygen overlay port: expected tool file missing: ${_tool}")
    endif()
endforeach()

file(INSTALL ${_tool_files}
    DESTINATION "${_tools_dir}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/copyright"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
