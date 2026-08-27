cmake_minimum_required(VERSION 3.16)
project(SeenShot VERSION 0.1.3 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(CURL REQUIRED)

# ─── Ariadne's Thread [AT-0100] ─────────────────────
# What: Fetch posthog-cpp v1.7.1 and require system libcurl
# Why:  PRD-06 — one official Client; HTTP disabled if CURL is missing
# Date: 2026-08-26
# Related: [AT-0101] Analytics.cpp, docs/PRD-06-analytics.md
# ─────────────────────────────────────────────────────
include(FetchContent)
set(POSTHOG_BUILD_TESTS OFF CACHE INTERNAL "")
FetchContent_Declare(
    posthog
    GIT_REPOSITORY https://github.com/baskl-ai/posthog-cpp.git
    GIT_TAG v1.7.1
)
FetchContent_MakeAvailable(posthog)

set(SEENSHOT_SOURCES
    client/src/main.cpp
    client/src/app/Application.cpp
    client/src/app/Analytics.cpp
    client/src/app/TrayController.cpp
    client/src/app/FirstRunWizard.cpp
    client/src/app/SettingsWindow.cpp
    client/src/app/AccountSignInPanel.cpp
    client/src/app/SignInDialog.cpp
    client/src/app/Logger.cpp
    client/src/app/Config.cpp
    client/src/app/MacPermissions.mm
    client/src/app/MacIcons.mm
    client/src/app/MacLoginItem.mm
    client/src/app/MacUrlHandler.mm
    client/src/errors/ErrorCatalog.cpp
    client/src/capture/RegionPicker.cpp
    client/src/capture/ScreenCaptureBackend.mm
    client/src/annotate/AnnotateWindow.cpp
    client/src/annotate/AnnotationCommands.cpp
    client/src/annotate/AnnotateItems.cpp
    client/src/camera/CameraCapture.mm
    client/src/camera/PersonCutout.mm
    client/src/export/CloudPngEncoder.cpp
    client/src/hotkey/MacHotkeyBackend.mm
    client/src/auth/AuthSession.cpp
    client/src/auth/FirebaseAuthClient.cpp
    client/src/auth/KeychainStore.cpp
    client/src/auth/MacOAuthClient.mm
    client/src/cloud/CloudClient.cpp
    client/src/local/LocalStore.cpp
    client/src/update/SparkleUpdater.mm
)

# ─── Ariadne's Thread [AT-0125] ─────────────────────
# What: Put SeenShot.icns in the app Resources
# Why:  CFBundleIconFile needs the file inside the bundle
# Date: 2026-08-26
# Related: [AT-0124] packaging/macos/Info.plist, packaging/macos/SeenShot.icns
# ─────────────────────────────────────────────────────
set(SEENSHOT_ICNS ${CMAKE_SOURCE_DIR}/packaging/macos/SeenShot.icns)
set_source_files_properties(${SEENSHOT_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_executable(SeenShot MACOSX_BUNDLE ${SEENSHOT_SOURCES} ${SEENSHOT_ICNS})

set_source_files_properties(
    client/src/camera/CameraCapture.mm
    client/src/camera/PersonCutout.mm
    client/src/app/MacLoginItem.mm
    client/src/app/MacUrlHandler.mm
    client/src/auth/MacOAuthClient.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

target_include_directories(SeenShot PRIVATE
    ${CMAKE_SOURCE_DIR}/client/src
)

target_link_libraries(SeenShot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    posthog
    "-framework ScreenCaptureKit"
    "-framework CoreGraphics"
    "-framework AppKit"
    "-framework Carbon"
    "-framework Security"
    "-framework Foundation"
    "-framework ServiceManagement"
    "-framework AVFoundation"
    "-framework Vision"
    "-framework CoreVideo"
    "-framework CoreMedia"
    "-framework QuartzCore"
    "-framework AuthenticationServices"
)

set_target_properties(SeenShot PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks;/opt/homebrew/opt/qtbase/lib"
    INSTALL_RPATH "@executable_path/../Frameworks"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME "SeenShot"
    MACOSX_BUNDLE_ICON_FILE SeenShot.icns
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.seenshot.app"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${CMAKE_SOURCE_DIR}/packaging/macos/SeenShot.entitlements
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development: alexign90@icloud.com (N9AN38ACU9)"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "N9AN38ACU9"
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/Sparkle/Sparkle.framework")
    target_compile_definitions(SeenShot PRIVATE SEENSHOT_HAS_SPARKLE=1)
    target_compile_options(SeenShot PRIVATE "-F${CMAKE_SOURCE_DIR}/third_party/Sparkle")
    target_link_libraries(SeenShot PRIVATE
        "-F${CMAKE_SOURCE_DIR}/third_party/Sparkle"
        "-framework Sparkle"
    )
    set_target_properties(SeenShot PROPERTIES
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/../Frameworks"
    )
    add_custom_command(TARGET SeenShot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:SeenShot>/Contents/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/third_party/Sparkle/Sparkle.framework"
            "$<TARGET_BUNDLE_DIR:SeenShot>/Contents/Frameworks/Sparkle.framework"
        COMMENT "Copy Sparkle.framework into app bundle"
    )
endif()

# ─── Ariadne's Thread [AT-0179] ─────────────────────
# What: Prefer Developer ID when CODESIGN_IDENTITY is unset
# Why:  Apple Development vs /Applications Developer ID were two TCC identities; Settings showed one switch on
# Date: 2026-08-26
# Related: [AT-0039] CMakeLists.txt, packaging/macos/package_sparkle.sh, docs/macos-screen-recording.md
# ─────────────────────────────────────────────────────
if(APPLE)
    set(SEENSHOT_CODESIGN_IDENTITY "$ENV{CODESIGN_IDENTITY}")
    if(SEENSHOT_CODESIGN_IDENTITY STREQUAL "")
        execute_process(
            COMMAND /usr/bin/security find-identity -v -p codesigning
            OUTPUT_VARIABLE _seenshot_cs_out
            ERROR_QUIET
        )
        if(_seenshot_cs_out MATCHES "\"(Developer ID Application: [^\"]+)\"")
            set(SEENSHOT_CODESIGN_IDENTITY "${CMAKE_MATCH_1}")
        else()
            set(SEENSHOT_CODESIGN_IDENTITY "97F09F2576E6D7A376A28502AFC71A9095FAADA5")
        endif()
    endif()
    message(STATUS "SeenShot codesign identity: ${SEENSHOT_CODESIGN_IDENTITY}")
    add_custom_command(TARGET SeenShot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "codesign SeenShot.app identity=${SEENSHOT_CODESIGN_IDENTITY}"
        COMMAND /usr/bin/codesign --force --sign "${SEENSHOT_CODESIGN_IDENTITY}"
            --entitlements "${CMAKE_SOURCE_DIR}/packaging/macos/SeenShot.entitlements"
            --options runtime --timestamp=none
            "$<TARGET_BUNDLE_DIR:SeenShot>"
        COMMENT "codesign SeenShot.app"
        VERBATIM
    )
endif()
