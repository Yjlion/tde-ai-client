include_directories(
  ${CMAKE_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TDE_INCLUDE_DIR}
  ${TQT_INCLUDE_DIRS}
)

link_directories(
  ${TQT_LIBRARY_DIRS}
  ${TDE_LIB_DIR}
)


##### tde-ai-client (executable)

tde_add_executable( tde-ai-client AUTOMOC

  SOURCES
    main.cpp
    mainwindow.cpp
    chatview.cpp
    inputedit.cpp
    attachment.cpp
    json.cpp
    httpclient.cpp
    backend.cpp
    hordebackend.cpp
    openaibackend.cpp
    koboldbackend.cpp
    settings.cpp
    settingsdialog.cpp
    conversationstore.cpp

  LINK
    tdecore-shared
    tdeui-shared
    tdeio-shared
    tdewalletclient-shared

  DESTINATION ${BIN_INSTALL_DIR}
)


##### other data

install( FILES tde-ai-clientui.rc DESTINATION ${DATA_INSTALL_DIR}/tde-ai-client )
tde_create_translated_desktop( tde-ai-client.desktop )
