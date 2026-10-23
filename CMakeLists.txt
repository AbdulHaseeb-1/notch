cmake_minimum_required(VERSION 3.21)

project(noch VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Multimedia Network ShaderTools)

qt_standard_project_setup()

# qt_policy() only exists once Qt6 has been found, so this must come after
# find_package() and before qt_add_qml_module().
#   QTP0001 - place the module under the default QML import path (:/qt/qml/)
#   QTP0004 - generate a qmldir for the qml/ subdirectory so its components
#             resolve without an extra explicit import
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Source folders are grouped by responsibility. Keep this list explicit so a
# new file is a deliberate addition and stale files do not silently compile.
set(NOCH_SOURCES
    src/main.cpp

    src/agent/AgentPolicy.cpp
    src/agent/AgentPolicyEvaluate.cpp
    src/agent/AgentPolicy.h
    src/agent/AgentPolicyInternal.h
    src/agent/AgentPolicyRules.cpp
    src/agent/AgentPolicySafety.cpp
    src/agent/AgentPolicyShell.cpp
    src/agent/BuddyProvider.cpp
    src/agent/BuddyProviderConfig.cpp
    src/agent/BuddyProvider.h
    src/agent/BuddyProviderInternal.h
    src/agent/BuddyProviderContext.cpp
    src/agent/BuddyProviderPlan.cpp
    src/agent/BuddyProviderPrompts.cpp
    src/agent/BuddyProviderProtocol.cpp
    src/agent/BuddyProviderRun.cpp
    src/agent/BuddyProviderStorage.cpp
    src/agent/BuddyProviderTools.cpp
    src/agent/BuddyProviderWeb.cpp
    src/agent/ComputerUse.cpp
    src/agent/ComputerUse.h
    src/agent/ComputerUseActions.cpp
    src/agent/ComputerUseCoordinates.cpp
    src/agent/ComputerUseKeys.cpp
    src/agent/ComputerUseKeys.h
    src/agent/ComputerUseWin.cpp
    src/agent/ProjectContext.cpp
    src/agent/ProjectContext.h
    src/agent/RunJournal.cpp
    src/agent/RunJournal.h
    src/agent/SavedRules.cpp
    src/agent/SavedRules.h
    src/agent/SessionLink.cpp
    src/agent/SessionLink.h
    src/agent/StreamAccumulator.cpp
    src/agent/ToolSchemas.cpp
    src/agent/ToolSchemas.h
    src/agent/PlanValidator.cpp
    src/agent/PlanValidator.h
    src/agent/HookBus.cpp
    src/agent/HookBus.h
    src/agent/MemoryStore.cpp
    src/agent/MemoryStore.h
    src/agent/MemoryExtractor.h
    src/agent/StreamAccumulator.h
    src/agent/UserQuestions.cpp
    src/agent/UserQuestions.h
    src/agent/TurnClassifier.cpp
    src/agent/TurnClassifier.h

    src/app/QmlSingletons.h

    src/chat/ChatSessions.cpp
    src/chat/ChatSessions.h
    src/chat/SessionFormat.h

    src/common/ImageAlpha.h
    src/common/ShapeRegion.h

    src/desktop/AppIconProvider.cpp
    src/desktop/AppIconProvider.h
    src/desktop/AppSearch.cpp
    src/desktop/AppSearch.h
    src/desktop/Calc.h
    src/desktop/ClipboardHistory.cpp
    src/desktop/ClipboardHistory.h
    src/desktop/DockController.cpp
    src/desktop/DockController.h
    src/desktop/IconSet.cpp
    src/desktop/IconSet.h
    src/desktop/RunningApps.cpp
    src/desktop/RunningApps.h

    src/media/AudioDevices.cpp
    src/media/AudioDevices.h
    src/media/MediaHub.cpp
    src/media/MusicAgentBackend.cpp
    src/media/MediaHub.h
    src/media/MusicPlayer.cpp
    src/media/MusicPlayer.h
    src/media/Radios.cpp
    src/media/Radios.h
    src/media/SmtcMedia.cpp
    src/media/SmtcMedia.h
    src/media/OnlineTrack.h
    src/media/YtDlp.cpp
    src/media/YtDlp.h

    src/platform/Appearance.cpp
    src/platform/Appearance.h
    src/platform/FullscreenWatcher.cpp
    src/platform/FullscreenWatcher.h
    src/platform/GlassBackdrop.cpp
    src/platform/GlassBackdrop.h
    src/platform/GlobalHotkey.cpp
    src/platform/GlobalHotkey.h
    src/platform/NightLight.cpp
    src/platform/NightLight.h
    src/platform/OverlayController.cpp
    src/platform/OverlayController.h
    src/platform/PowerActions.cpp
    src/platform/PowerActions.h
    src/platform/WinRtAbi.h

    src/voice/BuddySfx.cpp
    src/voice/BuddySfx.h
    src/voice/DeepgramAgent.cpp
    src/voice/DeepgramAgent.h
    src/voice/DeepgramProtocol.cpp
    src/voice/DeepgramProtocol.h
    src/voice/DeepgramTranscriber.cpp
    src/voice/Moonshine.cpp
    src/voice/Moonshine.h
    src/voice/MoonshineTranscriber.cpp
    src/voice/OrtRuntime.cpp
    src/voice/OrtRuntime.h
    src/voice/PushToTalkKey.cpp
    src/voice/PushToTalkKey.h
    src/voice/SpeechOut.cpp
    src/voice/SpeechOut.h
    src/voice/SpeechPlayer.cpp
    src/voice/SpeechPlayer.h
    src/voice/EchoCanceller.cpp
    src/voice/EchoCanceller.h
    src/voice/StreamingPlayer.cpp
    src/voice/StreamingPlayer.h
    src/voice/Transcriber.h
    src/voice/VoiceAudio.cpp
    src/voice/VoiceAudio.h
    src/voice/VoiceInput.cpp
    src/voice/VoiceInput.h
    src/voice/WakeDetector.cpp
    src/voice/WakeDetector.h
    src/voice/WakeWord.cpp
    src/voice/WakeWord.h
    src/voice/WsClient.cpp
    src/voice/WsClient.h
)
qt_add_executable(noch ${NOCH_SOURCES})

set(NOCH_SOURCE_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chat
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop
    ${CMAKE_CURRENT_SOURCE_DIR}/src/media
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/voice
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime
)
add_library(noch_includes INTERFACE)
target_include_directories(noch_includes INTERFACE ${NOCH_SOURCE_INCLUDE_DIRS})

target_compile_definitions(noch PRIVATE NOCH_VERSION="${PROJECT_VERSION}")

# Warnings are reported, not fatal: the tree is not yet warning-free, so -Werror
# would stop the build on existing code. Read the output and fix new ones.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(noch PRIVATE -Wall -Wextra -Wshadow -Wnon-virtual-dtor)
elseif(MSVC)
    target_compile_options(noch PRIVATE /W4)
endif()

# The QML files are compiled into the executable's resources, so the built
# noch.exe is self-contained - no qml/ folder needed beside it at run time.
# Sources live in subsystem folders, while the resource aliases keep the QML
# module flat (Main.qml, Buddy.qml, ...) so imports stay unchanged.
set(NOCH_QML_FILES
    qml/Main.qml

    qml/theme/Theme.qml

    qml/components/Flyout.qml
    qml/components/Glass.qml
    qml/components/Glyph.qml
    qml/components/MenuRow.qml
    qml/components/MoreButton.qml
    qml/components/PeekDash.qml
    qml/components/ResizeGrip.qml
    qml/components/Segmented.qml
    qml/components/SizeSlider.qml
    qml/components/Toggle.qml
    qml/components/TooltipToast.qml

    qml/panels/ClipboardView.qml
    qml/panels/FocusTimer.qml
    qml/panels/HubPanel.qml
    qml/panels/NowPlaying.qml
    qml/panels/PaletteCard.qml
    qml/panels/SettingsPanel.qml
    qml/panels/TimerPill.qml
    qml/panels/TimerView.qml

    qml/panels/settings/SettingsCardDivider.qml
    qml/panels/settings/SettingsChip.qml
    qml/panels/settings/SettingsFieldBox.qml
    qml/panels/settings/SettingsGroupCard.qml
    qml/panels/settings/SettingsIconButton.qml
    qml/panels/settings/SettingsLabeledSlider.qml
    qml/panels/settings/SettingsNotice.qml
    qml/panels/settings/SettingsOptionTile.qml
    qml/panels/settings/SettingsPage.qml
    qml/panels/settings/SettingsPillButton.qml
    qml/panels/settings/SettingsRingSwatch.qml
    qml/panels/settings/SettingsSectionHeader.qml
    qml/panels/settings/SettingsTextArea.qml
    qml/panels/settings/SettingsSidebarItem.qml
    qml/panels/settings/SettingsToggleRow.qml

    qml/panels/settings/pages/AgentPage.qml
    qml/panels/settings/pages/BuddyPage.qml
    qml/panels/settings/pages/DeepgramPage.qml
    qml/panels/settings/pages/GeneralPage.qml
    qml/panels/settings/pages/LookPage.qml
    qml/panels/settings/pages/ModelPage.qml
    qml/panels/settings/pages/SafetyPage.qml
    qml/panels/settings/pages/VoicePage.qml

    qml/flyouts/HubFlyout.qml
    qml/flyouts/PaletteFlyout.qml
    qml/flyouts/SettingsFlyout.qml

    qml/buddy/Buddy.qml
    qml/buddy/BuddyAppLaunch.qml
    qml/buddy/BuddyBubble.qml
    qml/buddy/BuddyChartCard.qml
    qml/buddy/BuddyHelpCard.qml
    qml/buddy/BuddyLiveCaption.qml
    qml/buddy/BuddyMenu.qml
    qml/buddy/BuddyNeonRing.qml
    qml/buddy/BuddyPanel.qml
    qml/buddy/BuddyQuestionCard.qml
    qml/buddy/BuddyReasoningCard.qml
    qml/buddy/BuddyResearchCard.qml
    qml/buddy/BuddyRouter.qml
    qml/buddy/BuddyTasksCard.qml
    qml/buddy/BuddyTodoCard.qml
    qml/buddy/ToolIcons.js

    qml/buddy/components/BuddyAskField.qml
    qml/buddy/components/BuddyIconButton.qml
    qml/buddy/components/BuddyMicButton.qml
    qml/buddy/components/BuddySendButton.qml
    qml/buddy/components/BuddyShot.qml

    qml/buddy/fx/BuddyArcRing.qml
    qml/buddy/fx/BuddyBlob.qml
    qml/buddy/fx/BuddyGlow.qml
    qml/buddy/fx/BuddyListenFx.qml
    qml/buddy/fx/BuddyRing.qml
    qml/buddy/fx/BuddyShaderRing.qml
    qml/buddy/fx/BuddySpeakFx.qml

    qml/agent/AgentControlOverlay.qml
    qml/agent/AgentCursor.qml
    qml/agent/AgentGlow.qml
    qml/agent/AgentHud.qml
    qml/agent/ToolApprovalCard.qml
    qml/agent/ToolStatusIcon.qml
    qml/agent/ToolStepCard.qml
    qml/agent/ToolStepRow.qml
    qml/agent/ToolTraceBar.qml
)

# Keep every type at the module root, regardless of its source folder.
foreach(qml_file IN LISTS NOCH_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
# The one dark stone theme: colours, type, spacing, shared by every file.
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The SVG icon set (qml/icons/*.svg, regenerated by tools/build_icons.py).
# IconSet.cpp reads them from :/qt/qml/noch/qml/icons at start-up.
file(GLOB NOCH_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/icons/*.svg)

# The wake word models (openWakeWord's two shared feature models and the
# "Hey Buddy" classifier, ~2.8 MB). They are read from the resources at start-up;
# a model dropped into %APPDATA%\noch\wakeword takes their place.
file(GLOB NOCH_WAKEWORD_MODELS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/wakeword/*.onnx)
set(NOCH_MOONSHINE_VOCAB resources/moonshine/vocab.json)

qt_add_qml_module(noch
    URI noch
    VERSION 1.0
    DEPENDENCIES QtQuick
    QML_FILES ${NOCH_QML_FILES}
    RESOURCES ${NOCH_ICONS} ${NOCH_WAKEWORD_MODELS} ${NOCH_MOONSHINE_VOCAB}
)

# Buddy's ring shader (GPU ring in Settings > Look & glow), compiled to .qsb for
# every graphics API and loaded as qrc:/shaders/*.qsb.
qt_add_shaders(noch "noch_shaders"
    PREFIX "/shaders"
    BASE shaders
    GLSL "150,300 es"
    HLSL 50
    MSL 12
    FILES
        shaders/buddyring.vert
        shaders/buddyring.frag
)

# Lets the generated QML type registration find src/app/QmlSingletons.h.
target_include_directories(noch PRIVATE ${NOCH_SOURCE_INCLUDE_DIRS})

target_link_libraries(noch PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    Qt6::Network
)

if(WIN32)
    target_link_libraries(noch PRIVATE user32 shell32 iphlpapi dwmapi ole32 powrprof)
endif()

set_target_properties(noch PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# ONNX Runtime runs the wake word models. Noch loads onnxruntime.dll when it is
# needed (see src/voice/OrtRuntime.cpp) instead of linking it, so the app builds
# and starts without it and the wake word then falls back to the speech service.
# On Windows the official build is downloaded once (hash-checked) and copied next
# to noch.exe. Turn this off to supply your own onnxruntime.dll.
option(NOCH_FETCH_ONNXRUNTIME "Download onnxruntime.dll for the local wake word (Windows)" ON)
if(WIN32 AND NOCH_FETCH_ONNXRUNTIME)
    set(NOCH_ORT_VERSION 1.20.1)
    set(NOCH_ORT_ZIP_SHA256 78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581)
    set(NOCH_ORT_DEPS ${CMAKE_BINARY_DIR}/_deps/onnxruntime)
    set(NOCH_ORT_DLL ${NOCH_ORT_DEPS}/onnxruntime.dll)
    if(NOT EXISTS ${NOCH_ORT_DLL})
        set(_ort_zip ${NOCH_ORT_DEPS}/onnxruntime-win-x64-${NOCH_ORT_VERSION}.zip)
        message(STATUS "Downloading ONNX Runtime ${NOCH_ORT_VERSION} for the wake word...")
        file(DOWNLOAD
            https://github.com/microsoft/onnxruntime/releases/download/v${NOCH_ORT_VERSION}/onnxruntime-win-x64-${NOCH_ORT_VERSION}.zip
            ${_ort_zip}
            EXPECTED_HASH SHA256=${NOCH_ORT_ZIP_SHA256}
            STATUS _ort_status)
        list(GET _ort_status 0 _ort_code)
        if(_ort_code EQUAL 0)
            file(ARCHIVE_EXTRACT INPUT ${_ort_zip} DESTINATION ${NOCH_ORT_DEPS}/unpacked
                 PATTERNS "*/lib/onnxruntime.dll")
            file(GLOB_RECURSE _ort_found ${NOCH_ORT_DEPS}/unpacked/*/onnxruntime.dll)
            if(_ort_found)
                list(GET _ort_found 0 _ort_first)
                file(COPY_FILE ${_ort_first} ${NOCH_ORT_DLL})
            endif()
            file(REMOVE ${_ort_zip})
        else()
            message(WARNING "Could not download ONNX Runtime (${_ort_status}). The wake word will check each "
                            "sentence with your speech service instead. Put onnxruntime.dll next to noch.exe "
                            "to use the local detector.")
        endif()
    endif()
    if(EXISTS ${NOCH_ORT_DLL})
        add_custom_command(TARGET noch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NOCH_ORT_DLL} $<TARGET_FILE_DIR:noch>)
    endif()
endif()

# Speech to text: Moonshine Tiny (MIT licence), the quantized ONNX export from
# onnx-community/moonshine-tiny-ONNX, run on this computer by ONNX Runtime. The two
# model files (about 28 MB) are downloaded once at configure time, hash-checked,
# and copied next to the executable (models/moonshine-tiny). Without them voice
# input says what is missing; they can also be put in %APPDATA%\noch\models\moonshine-tiny.
option(NOCH_FETCH_MOONSHINE "Download the Moonshine Tiny speech model (about 28 MB)" ON)
if(NOCH_FETCH_MOONSHINE)
    set(NOCH_MOONSHINE_REV a6da1241cd305dcd64eab1edbd615f2bb9aabb95)
    set(NOCH_MOONSHINE_URL https://huggingface.co/onnx-community/moonshine-tiny-ONNX/resolve/${NOCH_MOONSHINE_REV}/onnx)
    set(NOCH_MOONSHINE_DEPS ${CMAKE_BINARY_DIR}/_deps/moonshine-tiny)
    file(MAKE_DIRECTORY ${NOCH_MOONSHINE_DEPS})
    set(_moonshine_files
        "encoder.onnx|encoder_model_quantized.onnx|c6fc4b7bc5af75c0591fd157a1f3829b533d18e9769a888fd95a62e470dd4f4a"
        "decoder.onnx|decoder_model_merged_quantized.onnx|eed87831c3a6103534aae7d47a5d485025c659a1323901513961c39fe8a1a367")
    set(_moonshine_ok TRUE)
    foreach(_item IN LISTS _moonshine_files)
        string(REPLACE "|" ";" _parts "${_item}")
        list(GET _parts 0 _local)
        list(GET _parts 1 _remote)
        list(GET _parts 2 _hash)
        if(NOT EXISTS ${NOCH_MOONSHINE_DEPS}/${_local})
            message(STATUS "Downloading Moonshine Tiny (${_local})...")
            file(DOWNLOAD ${NOCH_MOONSHINE_URL}/${_remote} ${NOCH_MOONSHINE_DEPS}/${_local}
                 EXPECTED_HASH SHA256=${_hash} STATUS _status)
            list(GET _status 0 _code)
            if(NOT _code EQUAL 0)
                file(REMOVE ${NOCH_MOONSHINE_DEPS}/${_local})
                message(WARNING "Could not download the Moonshine model (${_status}). Voice input will ask for it: "
                                "put encoder.onnx and decoder.onnx in %APPDATA%\\noch\\models\\moonshine-tiny.")
                set(_moonshine_ok FALSE)
            endif()
        endif()
    endforeach()
    if(_moonshine_ok)
        add_custom_command(TARGET noch POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:noch>/models/moonshine-tiny
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NOCH_MOONSHINE_DEPS}/encoder.onnx
                    ${NOCH_MOONSHINE_DEPS}/decoder.onnx $<TARGET_FILE_DIR:noch>/models/moonshine-tiny)
    endif()
endif()

# Unit tests (pure-logic modules only): cmake -DNOCH_BUILD_TESTS=ON, then ctest.
option(NOCH_BUILD_TESTS "Build unit tests" OFF)
if(NOCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
