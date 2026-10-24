# Unit tests: configure with -DNOCH_BUILD_TESTS=ON and run ctest.
#
# Every suite links the per-area libraries from the root CMakeLists
# (noch_agent_core, noch_connectors, noch_agent, noch_voice, noch_media,
# noch_desktop, noch_chat) instead of recompiling the production sources
# it needs — each production translation unit is compiled exactly once,
# into its library, and shared by the app and all tests.
include(${PROJECT_SOURCE_DIR}/cmake/NochTest.cmake)

find_package(Qt6 REQUIRED COMPONENTS Test Network)

if(WIN32)
    set(NOCH_WIN_LIBS user32 crypt32)
else()
    set(NOCH_WIN_LIBS "")
endif()
if(WIN32)
    set(NOCH_CRYPT_LIBS crypt32)
else()
    set(NOCH_CRYPT_LIBS "")
endif()

noch_add_unit_test(tst_agentpolicy
    SOURCES tests/tst_agentpolicy.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_iconset
    SOURCES tests/tst_iconset.cpp
    LIBS noch_desktop Qt6::Core
    DEFS NOCH_ICON_DIR="${PROJECT_SOURCE_DIR}/qml/icons")

# The output guardrails as their own component (extracted from
# AgentPolicy); tst_agentpolicy pins the same behavior through the
# policy's forwarding statics.
noch_add_unit_test(tst_outputguard
    SOURCES tests/tst_outputguard.cpp
    LIBS noch_agent_core Qt6::Core)

noch_add_unit_test(tst_stream
    SOURCES tests/tst_stream.cpp
    LIBS noch_agent_core Qt6::Core)

noch_add_unit_test(tst_toolrepair
    SOURCES tests/tst_toolrepair.cpp
    # The test includes BuddyProvider.h for the tool-call declarations;
    # noch_agent brings the chat/connectors/media headers it pulls in.
    LIBS noch_agent noch_agent_core Qt6::Core Qt6::Gui Qt6::Network)

noch_add_unit_test(tst_buddysfx
    SOURCES tests/tst_buddysfx.cpp
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Multimedia)

noch_add_unit_test(tst_userquestions
    SOURCES tests/tst_userquestions.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_voice
    SOURCES tests/tst_voice.cpp
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia ${NOCH_WIN_LIBS}
    DEFS NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_wakeword
    SOURCES tests/tst_wakeword.cpp
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia
    DEFS NOCH_WAKEWORD_DIR="${PROJECT_SOURCE_DIR}/resources/wakeword"
         NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_wakedetector
    SOURCES tests/tst_wakedetector.cpp
    LIBS noch_voice Qt6::Core
    DEFS NOCH_WAKEWORD_DIR="${PROJECT_SOURCE_DIR}/resources/wakeword"
         NOCH_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")

noch_add_unit_test(tst_speechout
    SOURCES tests/tst_speechout.cpp
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

noch_add_unit_test(tst_chatsessions
    SOURCES tests/tst_chatsessions.cpp
    LIBS noch_chat noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_projectcontext
    SOURCES tests/tst_projectcontext.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_skills
    SOURCES tests/tst_skills.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network
    DEFS NOCH_BUILTIN_SKILLS_DIR="${PROJECT_SOURCE_DIR}/resources/skills")

noch_add_unit_test(tst_savedrules
    SOURCES tests/tst_savedrules.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_runjournal
    SOURCES tests/tst_runjournal.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

noch_add_unit_test(tst_buddyagent
    SOURCES tests/tst_buddyagent.cpp tests/BuddyAgentFakes.h
    LIBS noch_agent noch_connectors noch_agent_core noch_chat
         Qt6::Core Qt6::Gui Qt6::Network ${NOCH_WIN_LIBS})

noch_add_unit_test(tst_computeruse
    SOURCES tests/tst_computeruse.cpp
    LIBS noch_agent noch_agent_core Qt6::Core Qt6::Gui ${NOCH_WIN_LIBS})

noch_add_unit_test(tst_mem0client
    SOURCES tests/tst_mem0client.cpp
    LIBS noch_agent_core Qt6::Core Qt6::Network)

# Connectors: encrypted secret storage, OAuth (PKCE + loopback), Gmail,
# GitHub and MCP, against fake servers.
noch_add_unit_test(tst_secretstore
    SOURCES tests/tst_secretstore.cpp
    LIBS noch_connectors Qt6::Core ${NOCH_CRYPT_LIBS})

noch_add_unit_test(tst_oauth
    SOURCES tests/tst_oauth.cpp tests/FakeHttp.h
    LIBS noch_connectors Qt6::Core Qt6::Gui Qt6::Network)

noch_add_unit_test(tst_gmail
    SOURCES tests/tst_gmail.cpp tests/FakeHttp.h
    LIBS noch_connectors Qt6::Core Qt6::Gui Qt6::Network ${NOCH_CRYPT_LIBS})

noch_add_unit_test(tst_github
    SOURCES tests/tst_github.cpp tests/FakeHttp.h
    LIBS noch_connectors Qt6::Core Qt6::Gui Qt6::Network ${NOCH_CRYPT_LIBS})

noch_add_unit_test(tst_connectorregistry
    SOURCES tests/tst_connectorregistry.cpp tests/FakeHttp.h tests/FakeMcp.h
    LIBS noch_connectors Qt6::Core Qt6::Gui Qt6::Network ${NOCH_CRYPT_LIBS})

noch_add_unit_test(tst_mcp
    SOURCES tests/tst_mcp.cpp tests/FakeHttp.h tests/FakeMcp.h
    LIBS noch_connectors Qt6::Core Qt6::Network ${NOCH_CRYPT_LIBS})

# Buddy's music tool against the real player, media hub and Windows media sessions.
if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia)
    noch_add_unit_test(tst_musicagent
        SOURCES tests/tst_musicagent.cpp tests/FakeYtDlp.h
        LIBS noch_media noch_desktop
             Qt6::Core Qt6::Gui Qt6::Quick Qt6::Multimedia Qt6::Network user32 shell32 ole32 dwmapi)
    noch_add_unit_test(tst_ytdlp
        SOURCES tests/tst_ytdlp.cpp tests/FakeYtDlp.h
        LIBS noch_media Qt6::Core Qt6::Gui Qt6::Network)
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
    LIBS noch_voice Qt6::Core Qt6::Network)

noch_add_unit_test(tst_echo
    SOURCES tests/tst_echo.cpp
    LIBS noch_voice Qt6::Core)

noch_add_unit_test(tst_deepgram
    SOURCES tests/tst_deepgram.cpp tests/SpeechFakes.h tests/WsTestServer.h
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

noch_add_unit_test(tst_deepgramrest
    SOURCES tests/tst_deepgramrest.cpp tests/SpeechFakes.h
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)

# The same against the real service. Skipped unless DEEPGRAM_API_KEY is set, so the
# key stays out of the repository and the normal run stays offline.
noch_add_unit_test(tst_deepgramlive
    SOURCES tests/tst_deepgramlive.cpp tests/SpeechFakes.h
    LIBS noch_voice Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia)
