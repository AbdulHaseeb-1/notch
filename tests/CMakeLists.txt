# Unit tests: configure with -DNOCH_BUILD_TESTS=ON and run ctest.
include(${PROJECT_SOURCE_DIR}/cmake/NochTest.cmake)

find_package(Qt6 REQUIRED COMPONENTS Test Network)

# AgentPolicy is split by responsibility; every test that exercises it needs
# the whole unit, so keep the source list in one place.
set(NOCH_AGENT_POLICY_SOURCES
    src/agent/AgentPolicy.cpp
    src/agent/AgentPolicyEvaluate.cpp
    src/agent/AgentPolicyInternal.h
    src/agent/AgentPolicyRules.cpp
    src/agent/AgentPolicySafety.cpp
    src/agent/AgentPolicyShell.cpp
    src/agent/ComputerUseKeys.cpp
    src/agent/ComputerUseKeys.h
)

# Computer-use parsing, coordinate mapping and the controller are separate
# translation units; tests need all three plus the platform backend.
set(NOCH_COMPUTER_USE_SOURCES
    src/agent/ComputerUse.cpp
    src/agent/ComputerUse.h
    src/agent/ComputerUseActions.cpp
    src/agent/ComputerUseCoordinates.cpp
    src/agent/ComputerUseKeys.cpp
    src/agent/ComputerUseKeys.h
    src/agent/ComputerUseWin.cpp
)

# Speech to text: VoiceInput, its Moonshine engine and the ONNX Runtime binding.
set(NOCH_SPEECH_TO_TEXT_SOURCES
    src/voice/VoiceInput.cpp
    src/voice/VoiceInput.h
    src/voice/Transcriber.h
    src/voice/MoonshineTranscriber.cpp
    src/voice/DeepgramTranscriber.cpp
    src/voice/DeepgramProtocol.cpp
    src/voice/DeepgramProtocol.h
    src/voice/Moonshine.cpp
    src/voice/Moonshine.h
    src/voice/OrtRuntime.cpp
    src/voice/OrtRuntime.h
)

if(WIN32)
    set(NOCH_WIN_LIBS user32)
else()
    set(NOCH_WIN_LIBS "")
endif()

noch_add_unit_test(tst_agentpolicy
    SOURCES tests/tst_agentpolicy.cpp
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_iconset
    SOURCES tests/tst_iconset.cpp src/desktop/IconSet.cpp src/desktop/IconSet.h
    LIBS Qt6::Core
    DEFS NOCH_ICON_DIR="${PROJECT_SOURCE_DIR}/qml/icons")

noch_add_unit_test(tst_stream
    SOURCES tests/tst_stream.cpp
            src/agent/StreamAccumulator.cpp src/agent/StreamAccumulator.h
    LIBS Qt6::Core)

noch_add_unit_test(tst_toolrepair
    SOURCES tests/tst_toolrepair.cpp
            src/agent/ToolSchemas.cpp src/agent/ToolSchemas.h
            src/agent/PlanValidator.cpp src/agent/PlanValidator.h
            src/agent/HookBus.cpp src/agent/HookBus.h
            src/agent/MemoryStore.cpp src/agent/MemoryStore.h src/agent/MemoryExtractor.h
    LIBS Qt6::Core Qt6::Gui Qt6::Network)

noch_add_unit_test(tst_buddysfx
    SOURCES tests/tst_buddysfx.cpp src/voice/BuddySfx.cpp src/voice/BuddySfx.h
    LIBS Qt6::Core Qt6::Gui Qt6::Multimedia)

noch_add_unit_test(tst_userquestions
    SOURCES tests/tst_userquestions.cpp
            src/agent/UserQuestions.cpp src/agent/UserQuestions.h
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_voice
    SOURCES tests/tst_voice.cpp
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
            src/voice/PushToTalkKey.cpp src/voice/PushToTalkKey.h
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia ${NOCH_WIN_LIBS}
    DEFS NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_wakeword
    SOURCES tests/tst_wakeword.cpp
            src/voice/StreamingPlayer.cpp src/voice/StreamingPlayer.h
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/WakeDetector.cpp src/voice/WakeDetector.h
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/SpeechPlayer.cpp src/voice/SpeechPlayer.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia
    DEFS NOCH_WAKEWORD_DIR="${PROJECT_SOURCE_DIR}/resources/wakeword"
         NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_wakedetector
    SOURCES tests/tst_wakedetector.cpp
            src/voice/WakeDetector.cpp src/voice/WakeDetector.h
            src/voice/OrtRuntime.cpp src/voice/OrtRuntime.h
    LIBS Qt6::Core
    DEFS NOCH_WAKEWORD_DIR="${PROJECT_SOURCE_DIR}/resources/wakeword"
         NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_moonshine
    SOURCES tests/tst_moonshine.cpp
            src/voice/Moonshine.cpp src/voice/Moonshine.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            src/voice/OrtRuntime.cpp src/voice/OrtRuntime.h
    LIBS Qt6::Core Qt6::Multimedia
    DEFS NOCH_MOONSHINE_VOCAB="${PROJECT_SOURCE_DIR}/resources/moonshine/vocab.json"
         NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_speechout
    SOURCES tests/tst_speechout.cpp
            src/voice/StreamingPlayer.cpp src/voice/StreamingPlayer.h
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/SpeechPlayer.cpp src/voice/SpeechPlayer.h
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/WakeDetector.cpp src/voice/WakeDetector.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

noch_add_unit_test(tst_chatsessions
    SOURCES tests/tst_chatsessions.cpp
            src/chat/ChatSessions.cpp src/chat/ChatSessions.h src/chat/SessionFormat.h
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_projectcontext
    SOURCES tests/tst_projectcontext.cpp
            src/agent/ProjectContext.cpp src/agent/ProjectContext.h
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_savedrules
    SOURCES tests/tst_savedrules.cpp
            src/agent/SavedRules.cpp src/agent/SavedRules.h
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_runjournal
    SOURCES tests/tst_runjournal.cpp
            src/agent/RunJournal.cpp src/agent/RunJournal.h
            ${NOCH_AGENT_POLICY_SOURCES}
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_buddyagent
    SOURCES tests/tst_buddyagent.cpp tests/BuddyAgentFakes.h
            src/agent/BuddyProvider.cpp src/agent/BuddyProvider.h
            src/agent/BuddyProviderConfig.cpp src/agent/BuddyProviderInternal.h
            src/agent/BuddyProviderContext.cpp src/agent/BuddyProviderPlan.cpp
            src/agent/BuddyProviderPrompts.cpp src/agent/BuddyProviderProtocol.cpp
            src/agent/BuddyProviderRun.cpp src/agent/BuddyProviderStorage.cpp
            src/agent/BuddyProviderTools.cpp src/agent/BuddyProviderWeb.cpp
            src/agent/ToolSchemas.cpp src/agent/ToolSchemas.h
            src/agent/PlanValidator.cpp src/agent/PlanValidator.h
            src/agent/HookBus.cpp src/agent/HookBus.h
            src/agent/MemoryStore.cpp src/agent/MemoryStore.h src/agent/MemoryExtractor.h
            src/agent/TurnClassifier.cpp src/agent/TurnClassifier.h
            ${NOCH_AGENT_POLICY_SOURCES}
            src/agent/UserQuestions.cpp src/agent/UserQuestions.h
            src/agent/RunJournal.cpp src/agent/RunJournal.h
            src/agent/SavedRules.cpp src/agent/SavedRules.h
            src/agent/ProjectContext.cpp src/agent/ProjectContext.h
            src/agent/SessionLink.cpp src/agent/SessionLink.h
            src/agent/StreamAccumulator.cpp src/agent/StreamAccumulator.h
            ${NOCH_COMPUTER_USE_SOURCES}
            src/chat/ChatSessions.cpp src/chat/ChatSessions.h src/chat/SessionFormat.h
    LIBS Qt6::Core Qt6::Gui Qt6::Network ${NOCH_WIN_LIBS})

noch_add_unit_test(tst_computeruse
    SOURCES tests/tst_computeruse.cpp
            ${NOCH_COMPUTER_USE_SOURCES}
    LIBS Qt6::Core Qt6::Gui ${NOCH_WIN_LIBS})

noch_add_unit_test(tst_turnclassifier
    SOURCES tests/tst_turnclassifier.cpp
            src/agent/TurnClassifier.cpp src/agent/TurnClassifier.h
            src/agent/ToolSchemas.cpp src/agent/ToolSchemas.h
    LIBS Qt6::Core Qt6::Network)

# Buddy's music tool against the real player, media hub and Windows media sessions.
if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia)
    noch_add_unit_test(tst_musicagent
        SOURCES tests/tst_musicagent.cpp tests/FakeYtDlp.h
                src/media/YtDlp.cpp src/media/YtDlp.h src/media/OnlineTrack.h
                src/media/MusicAgentBackend.cpp src/media/MusicAgentBackend.h
                src/media/MediaHub.cpp src/media/MediaHub.h
                src/media/MusicPlayer.cpp src/media/MusicPlayer.h
                src/media/SmtcMedia.cpp src/media/SmtcMedia.h
                src/media/AudioDevices.cpp src/media/AudioDevices.h
                src/desktop/RunningApps.cpp src/desktop/RunningApps.h
                src/desktop/WindowMonitor.cpp src/desktop/WindowMonitor.h
        LIBS Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Network user32 shell32 ole32 dwmapi)
    noch_add_unit_test(tst_ytdlp
        SOURCES tests/tst_ytdlp.cpp tests/FakeYtDlp.h
                src/media/YtDlp.cpp src/media/YtDlp.h src/media/OnlineTrack.h
        LIBS Qt6::Core Qt6::Gui Qt6::Network)
endif()

# QML component tests (tests/qml): run with Qt's qmltestrunner when installed.
find_program(NOCH_QMLTESTRUNNER NAMES qmltestrunner qmltestrunner-qt6
             HINTS "${Qt6_DIR}/../../../bin" "${QT6_INSTALL_PREFIX}/bin" /usr/lib/qt6/bin)
if(NOCH_QMLTESTRUNNER AND NOT WIN32)
    add_test(NAME tst_qml
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/qml/run_qml_tests.sh
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/qmltests ${NOCH_QMLTESTRUNNER})
else()
    message(STATUS "qmltestrunner not found (or Windows): QML tests are not registered")
endif()

# Deepgram: the WebSocket client, the wire protocol and the live voice session.
noch_add_unit_test(tst_wsclient
    SOURCES tests/tst_wsclient.cpp tests/WsTestServer.h
            src/voice/WsClient.cpp src/voice/WsClient.h
    LIBS Qt6::Core Qt6::Network)

noch_add_unit_test(tst_echo
    SOURCES tests/tst_echo.cpp
            src/voice/EchoCanceller.cpp src/voice/EchoCanceller.h
    LIBS Qt6::Core)

noch_add_unit_test(tst_deepgram
    SOURCES tests/tst_deepgram.cpp tests/SpeechFakes.h tests/WsTestServer.h
            src/voice/DeepgramAgent.cpp src/voice/DeepgramAgent.h
            src/voice/EchoCanceller.cpp src/voice/EchoCanceller.h
            src/voice/StreamingPlayer.cpp src/voice/StreamingPlayer.h
            src/voice/WsClient.cpp src/voice/WsClient.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

noch_add_unit_test(tst_deepgramrest
    SOURCES tests/tst_deepgramrest.cpp tests/SpeechFakes.h
            src/voice/StreamingPlayer.cpp src/voice/StreamingPlayer.h
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/SpeechPlayer.cpp src/voice/SpeechPlayer.h
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/WakeDetector.cpp src/voice/WakeDetector.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

# The same against the real service. Skipped unless DEEPGRAM_API_KEY is set, so the
# key stays out of the repository and the normal run stays offline.
noch_add_unit_test(tst_deepgramlive
    SOURCES tests/tst_deepgramlive.cpp tests/SpeechFakes.h
            src/voice/DeepgramAgent.cpp src/voice/DeepgramAgent.h
            src/voice/EchoCanceller.cpp src/voice/EchoCanceller.h
            src/voice/StreamingPlayer.cpp src/voice/StreamingPlayer.h
            src/voice/WsClient.cpp src/voice/WsClient.h
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/SpeechPlayer.cpp src/voice/SpeechPlayer.h
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/WakeDetector.cpp src/voice/WakeDetector.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            ${NOCH_SPEECH_TO_TEXT_SOURCES}
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)
