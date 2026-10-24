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

# One warning set for the app and every area library (see the note at the
# bottom of this block: warnings are reported, not fatal).
function(noch_apply_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wnon-virtual-dtor)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

# Source folders are grouped by responsibility. Keep this list explicit so a
# new file is a deliberate addition and stale files do not silently compile.
# The application sources are organised as per-area libraries below; the
# executable itself only contributes main.cpp and the QML registration
# header. Keep the library source lists explicit so a new file is a
# deliberate addition and stale files do not silently compile.

# --- Per-area static libraries -------------------------------------------------
# The folders under src/ are built as one static library per area instead of
# one monolithic executable target. Each library carries its own area's
# include directory PUBLIC, so a translation unit can only include another
# area's headers when it links that area's library — the layering in
# docs/architecture.md is enforced by the build, not by convention. The
# tests link these libraries too (see tests/CMakeLists.txt), so every
# production translation unit is compiled exactly once.

# common: header-only helpers, no dependencies.
add_library(noch_common INTERFACE
    src/common/ImageAlpha.h
    src/common/ShapeRegion.h
)
target_include_directories(noch_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/common)

# desktop: app search, clipboard, dock, running apps. No cross-area includes.
add_library(noch_desktop STATIC
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
    src/desktop/WindowMonitor.cpp
    src/desktop/WindowMonitor.h
)
target_include_directories(noch_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop)
target_link_libraries(noch_desktop PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
if(WIN32)
    # WindowMonitor asks DWM for cloak state and extended frame bounds.
    target_link_libraries(noch_desktop PUBLIC dwmapi)
endif()
noch_apply_warnings(noch_desktop)

# platform: windowing, hotkeys, appearance, power. Uses desktop's
# WindowMonitor (FullscreenWatcher.cpp) and common's ShapeRegion
# (OverlayController.cpp) from .cpp files only, so both links are PRIVATE.
add_library(noch_platform STATIC
    src/platform/Appearance.cpp
    src/platform/Appearance.h
    src/platform/FullscreenWatcher.cpp
    src/platform/FullscreenWatcher.h
    src/platform/GlobalHotkey.cpp
    src/platform/GlobalHotkey.h
    src/platform/OverlayController.cpp
    src/platform/OverlayController.h
    src/platform/PowerActions.cpp
    src/platform/PowerActions.h
    src/platform/WinRtAbi.h
)
target_include_directories(noch_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/platform)
target_link_libraries(noch_platform PUBLIC Qt6::Core Qt6::Gui
                      PRIVATE noch_common noch_desktop)
noch_apply_warnings(noch_platform)

# media: player, hub, Windows media sessions, yt-dlp. Cross-area uses are
# .cpp-only (ImageAlpha in MusicPlayer/SmtcMedia, RunningApps in MediaHub,
# WinRtAbi in SmtcMedia), so those links are PRIVATE. MusicBackend.h — the
# interface the agent consumes and MusicAgentBackend implements — lives
# here with its implementer (it was in src/agent and in no CMake list).
add_library(noch_media STATIC
    src/media/AudioDevices.cpp
    src/media/AudioDevices.h
    src/media/MediaHub.cpp
    src/media/MediaHub.h
    src/media/MusicAgentBackend.cpp
    src/media/MusicAgentBackend.h
    src/media/MusicBackend.h
    src/media/MusicPlayer.cpp
    src/media/MusicPlayer.h
    src/media/OnlineTrack.h
    src/media/SmtcMedia.cpp
    src/media/SmtcMedia.h
    src/media/YtDlp.cpp
    src/media/YtDlp.h
)
target_include_directories(noch_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/media)
target_link_libraries(noch_media PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia Qt6::Quick
                      PRIVATE noch_common noch_desktop noch_platform)
noch_apply_warnings(noch_media)

# voice: speech in/out, wake word, live agent. No cross-area includes at
# all. The vendored ONNX Runtime headers are needed by OrtRuntime.cpp
# only, so that include dir stays PRIVATE to this library.
add_library(noch_voice STATIC
    src/voice/BuddySfx.cpp
    src/voice/BuddySfx.h
    src/voice/DeepgramAgent.cpp
    src/voice/DeepgramAgent.h
    src/voice/DeepgramProtocol.cpp
    src/voice/DeepgramProtocol.h
    src/voice/DeepgramTranscriber.cpp
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
target_include_directories(noch_voice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/voice
                      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
target_link_libraries(noch_voice PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)
noch_apply_warnings(noch_voice)

# agent core: the policy engine and the provider-independent agent
# machinery (tools, plans, hooks, memory, skills, turn classification).
# Nothing in here includes a connector or provider header — the include
# scan shows a clean DAG core <- connectors <- agent. Note all three
# agent libraries share the flat src/agent directory, so within-agent
# layering is enforced by the link graph, not by include dirs.
add_library(noch_agent_core STATIC
    src/agent/AgentPolicy.cpp
    src/agent/AgentPolicyEvaluate.cpp
    src/agent/AgentPolicy.h
    src/agent/AgentPolicyInternal.h
    src/agent/AgentPolicyRules.cpp
    src/agent/AgentPolicySafety.cpp
    src/agent/AgentPolicyShell.cpp
    src/agent/ComputerUseKeys.cpp
    src/agent/ComputerUseKeys.h
    src/agent/HookBus.cpp
    src/agent/HookBus.h
    src/agent/Mem0Client.cpp
    src/agent/Mem0Client.h
    src/agent/MemoryExtractor.h
    src/agent/MemoryStore.cpp
    src/agent/MemoryStore.h
    src/agent/OutputGuard.cpp
    src/agent/OutputGuard.h
    src/agent/PlanValidator.cpp
    src/agent/PlanValidator.h
    src/agent/ProjectContext.cpp
    src/agent/ProjectContext.h
    src/agent/RunJournal.cpp
    src/agent/RunJournal.h
    src/agent/SavedRules.cpp
    src/agent/SavedRules.h
    src/agent/Skills.cpp
    src/agent/Skills.h
    src/agent/StreamAccumulator.cpp
    src/agent/StreamAccumulator.h
    src/agent/ToolSchemas.cpp
    src/agent/ToolSchemas.h
    src/agent/UserQuestions.cpp
    src/agent/UserQuestions.h
)
target_include_directories(noch_agent_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/agent)
target_link_libraries(noch_agent_core PUBLIC Qt6::Core Qt6::Network)
noch_apply_warnings(noch_agent_core)

# chat: session storage. Its only cross-area use is
# AgentPolicy::stripInvisible from ChatSessions.cpp, so agent_core is a
# PRIVATE link (the dependency points at core, never at the provider).
add_library(noch_chat STATIC
    src/chat/ChatSessions.cpp
    src/chat/ChatSessions.h
    src/chat/SessionFormat.h
)
target_include_directories(noch_chat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/chat)
target_link_libraries(noch_chat PUBLIC Qt6::Core
                      PRIVATE noch_agent_core)
noch_apply_warnings(noch_chat)

# connectors: the built-in apps (Gmail, GitHub), MCP servers, OAuth and
# the secret store. Depends only on agent_core (Connector.h includes
# ToolSchemas.h) — this is the plugin seam: a new provider is a new
# Connector implementation in this library, registered with
# ConnectorRegistry, and nothing else in the app needs to know.
add_library(noch_connectors STATIC
    src/agent/BuiltinOAuth.cpp
    src/agent/BuiltinOAuth.h
    src/agent/Connector.cpp
    src/agent/Connector.h
    src/agent/ConnectorRegistry.cpp
    src/agent/ConnectorRegistry.h
    src/agent/DeviceFlowClient.cpp
    src/agent/DeviceFlowClient.h
    src/agent/GithubConnector.cpp
    src/agent/GithubConnector.h
    src/agent/GmailConnector.cpp
    src/agent/GmailConnector.h
    src/agent/McpConnector.cpp
    src/agent/McpConnector.h
    src/agent/OAuthClient.cpp
    src/agent/OAuthClient.h
    src/agent/SecretStore.cpp
    src/agent/SecretStore.h
)
target_include_directories(noch_connectors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/agent)
target_link_libraries(noch_connectors PUBLIC noch_agent_core Qt6::Core Qt6::Gui Qt6::Network)
# McpConnector reports this as the client version; the app target has
# always compiled it with the definition, so the library carries it too.
target_compile_definitions(noch_connectors PRIVATE NOCH_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(noch_connectors PRIVATE crypt32)   # SecretStore DPAPI
endif()
noch_apply_warnings(noch_connectors)

# agent: the QML-facing BuddyProvider, computer use and the session
# link. Sits on top of agent_core, connectors, chat and media (the
# MusicBackend interface in BuddyProvider.h) — everything it includes
# from those areas arrives through these PUBLIC links.
add_library(noch_agent STATIC
    src/agent/BuddyProvider.cpp
    src/agent/BuddyProvider.h
    src/agent/BuddyProviderConfig.cpp
    src/agent/BuddyProviderConnectors.cpp
    src/agent/BuddyProviderContext.cpp
    src/agent/BuddyProviderInternal.h
    src/agent/BuddyProviderPlan.cpp
    src/agent/BuddyProviderPrompts.cpp
    src/agent/BuddyProviderProtocol.cpp
    src/agent/BuddyProviderRun.cpp
    src/agent/BuddyProviderSkills.cpp
    src/agent/BuddyProviderStorage.cpp
    src/agent/BuddyProviderTools.cpp
    src/agent/BuddyProviderWeb.cpp
    src/agent/ComputerUse.cpp
    src/agent/ComputerUse.h
    src/agent/ComputerUseActions.cpp
    src/agent/ComputerUseCoordinates.cpp
    src/agent/ComputerUseWin.cpp
    src/agent/SessionLink.cpp
    src/agent/SessionLink.h
)
target_include_directories(noch_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/agent)
target_link_libraries(noch_agent PUBLIC noch_agent_core noch_connectors noch_chat noch_media
                             Qt6::Core Qt6::Gui Qt6::Network)
noch_apply_warnings(noch_agent)

qt_add_executable(noch src/main.cpp src/app/QmlSingletons.h)

target_compile_definitions(noch PRIVATE NOCH_VERSION="${PROJECT_VERSION}")

# Warnings are reported, not fatal: the tree is not yet warning-free, so -Werror
# would stop the build on existing code. Read the output and fix new ones.
noch_apply_warnings(noch)

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
    qml/panels/settings/pages/ConnectorsPage.qml
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
# Buddy's built-in Agent Skills (resources/skills/<name>/SKILL.md), read from the resources
# by src/agent/Skills.cpp (see skills::builtinSkillsDir).
file(GLOB_RECURSE NOCH_BUILTIN_SKILLS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/skills/*)

qt_add_qml_module(noch
    URI noch
    VERSION 1.0
    DEPENDENCIES QtQuick
    QML_FILES ${NOCH_QML_FILES}
    RESOURCES ${NOCH_ICONS} ${NOCH_WAKEWORD_MODELS} ${NOCH_BUILTIN_SKILLS}
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
# Every other header main.cpp and the registration code need arrives
# through the area libraries linked above — the flat all-areas include
# list (the old noch_includes target) is gone entirely.
target_include_directories(noch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/app)

target_link_libraries(noch PRIVATE
    noch_common
    noch_desktop
    noch_platform
    noch_media
    noch_voice
    noch_agent_core
    noch_chat
    noch_connectors
    noch_agent
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Multimedia
    Qt6::Network
)

if(WIN32)
    target_link_libraries(noch PRIVATE user32 shell32 iphlpapi dwmapi ole32 powrprof crypt32)
endif()

set_target_properties(noch PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# qt_add_qml_module() puts the module's files in a "noch/" directory inside the
# build dir. On Windows the executable is noch.exe and never collides with it;
# on Linux the extensionless output file IS "noch", so the link fails with
# "cannot open output file noch: Is a directory". Keep the Windows layout
# (run.bat deploys build\noch.exe) and move only the non-Windows output.
if(NOT WIN32)
    set_target_properties(noch PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

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

# Unit tests (pure-logic modules only): cmake -DNOCH_BUILD_TESTS=ON, then ctest.
option(NOCH_BUILD_TESTS "Build unit tests" OFF)
if(NOCH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
