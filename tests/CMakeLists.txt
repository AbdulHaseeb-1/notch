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
)

# Computer-use parsing, coordinate mapping and the controller are separate
# translation units; tests need all three plus the platform backend.
set(NOCH_COMPUTER_USE_SOURCES
    src/agent/ComputerUse.cpp
    src/agent/ComputerUse.h
    src/agent/ComputerUseActions.cpp
    src/agent/ComputerUseCoordinates.cpp
    src/agent/ComputerUseWin.cpp
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
            src/voice/VoiceInput.cpp src/voice/VoiceInput.h
            src/voice/PushToTalkKey.cpp src/voice/PushToTalkKey.h
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia ${NOCH_WIN_LIBS})

noch_add_unit_test(tst_wakeword
    SOURCES tests/tst_wakeword.cpp
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            src/voice/VoiceInput.cpp src/voice/VoiceInput.h
    LIBS Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

noch_add_unit_test(tst_speechout
    SOURCES tests/tst_speechout.cpp
            src/voice/SpeechOut.cpp src/voice/SpeechOut.h
            src/voice/WakeWord.cpp src/voice/WakeWord.h
            src/voice/VoiceAudio.cpp src/voice/VoiceAudio.h
            src/voice/VoiceInput.cpp src/voice/VoiceInput.h
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
    SOURCES tests/tst_buddyagent.cpp
            src/agent/BuddyProvider.cpp src/agent/BuddyProvider.h
            src/agent/BuddyProviderConfig.cpp src/agent/BuddyProviderInternal.h
            src/agent/BuddyProviderPrompts.cpp src/agent/BuddyProviderProtocol.cpp
            src/agent/BuddyProviderRun.cpp src/agent/BuddyProviderStorage.cpp
            src/agent/BuddyProviderTools.cpp src/agent/BuddyProviderWeb.cpp
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
