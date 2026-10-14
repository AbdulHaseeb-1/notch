cmake_minimum_required(VERSION 3.21)

project(noch VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Multimedia)

qt_standard_project_setup()

# qt_policy() only exists once Qt6 has been found, so this must come after
# find_package() and before qt_add_qml_module().
#   QTP0001 - place the module under the default QML import path (:/qt/qml/)
#   QTP0004 - generate a qmldir for the qml/ subdirectory so its components
#             resolve without an extra explicit import
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(noch
    src/main.cpp
    src/OverlayController.cpp
    src/OverlayController.h
    src/DockController.cpp
    src/DockController.h
    src/AppIconProvider.cpp
    src/AppIconProvider.h
    src/SystemMonitor.cpp
    src/SystemMonitor.h
    src/MusicPlayer.cpp
    src/MusicPlayer.h
)

target_compile_definitions(noch PRIVATE NOCH_VERSION="${PROJECT_VERSION}")

# The QML files are compiled into the executable's resources, so the built
# noch.exe is self-contained - no qml/ folder needed beside it at run time.
qt_add_qml_module(noch
    URI noch
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/DockItem.qml
        qml/MoreButton.qml
        qml/SettingsPanel.qml
        qml/Toggle.qml
        qml/Segmented.qml
        qml/SizeSlider.qml
        qml/MenuRow.qml
        qml/Glyph.qml
        qml/StatusBlock.qml
        qml/NowPlaying.qml
        qml/SystemPanel.qml
        qml/MusicPanel.qml
)

target_link_libraries(noch PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
)

if(WIN32)
    target_link_libraries(noch PRIVATE user32 shell32 iphlpapi)
endif()

set_target_properties(noch PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
