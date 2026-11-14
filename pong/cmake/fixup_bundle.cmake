
include(BundleUtilities)

get_filename_component(
    APP_BUNDLE
    "${CMAKE_CURRENT_LIST_DIR}/../build/pong.app"
    REALPATH
)

fixup_bundle(
    "${APP_BUNDLE}"
    ""
    "/opt/homebrew/lib;/opt/homebrew/opt/sfml/lib;/opt/homebrew/opt/freetype/lib;/opt/homebrew/opt/libvorbis/lib;/opt/homebrew/opt/flac/lib;/opt/homebrew/opt/libogg/lib"
)

# On macOS Apple Silicon (arm64), install_name_tool invalidates dylib signatures.
# dyld will kill the app (SIGKILL 137) unless extended attributes are cleared
# and the entire bundle is re-signed.
message(STATUS "Fixing permissions, clearing extended attributes and re-signing bundle: ${APP_BUNDLE}")
execute_process(
    COMMAND chmod -R u+w "${APP_BUNDLE}"
    RESULT_VARIABLE CHMOD_RES
)
execute_process(
    COMMAND xattr -cr "${APP_BUNDLE}"
    RESULT_VARIABLE XATTR_RES
)
execute_process(
    COMMAND codesign --force --deep -s - "${APP_BUNDLE}"
    RESULT_VARIABLE CODESIGN_RES
)
if(NOT CODESIGN_RES EQUAL 0)
    message(WARNING "Failed to re-sign bundle (${CODESIGN_RES})")
endif()
