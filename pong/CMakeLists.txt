
cmake_minimum_required(VERSION 3.20)

project(Pong LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 3 COMPONENTS Graphics Window System Audio Network REQUIRED)

# Create executable
if(APPLE)
    add_executable(pong MACOSX_BUNDLE
        main.cpp
        Ball.cpp
        Paddle.cpp
        Ai.cpp
        Network.cpp
        ResourcePath.cpp
    )
    set_target_properties(pong PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Online Pong"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aayushsharma.onlinepong"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    )

    # Tell macOS to look for bundled libraries
    set_target_properties(pong PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

else()
    add_executable(pong
        main.cpp
        Ball.cpp
        Paddle.cpp
        Ai.cpp
        Network.cpp
        ResourcePath.cpp
    )
endif()

# Link SFML
target_link_libraries(pong PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
    SFML::Network
)

# Set asset destination
if(APPLE)
    set(ASSET_DESTINATION
        "$<TARGET_BUNDLE_CONTENT_DIR:pong>/Resources/assets"
    )
else()
    set(ASSET_DESTINATION
        "$<TARGET_FILE_DIR:pong>/assets"
    )
endif()

# Copy assets after building
add_custom_command(TARGET pong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${ASSET_DESTINATION}"
    COMMENT "Copying assets"
)
if(APPLE)
    add_custom_command(TARGET pong POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCMAKE_CURRENT_LIST_DIR=${CMAKE_SOURCE_DIR}/cmake
            -P ${CMAKE_SOURCE_DIR}/cmake/fixup_bundle.cmake
        COMMENT "Bundling SFML dependencies"
    )
endif()