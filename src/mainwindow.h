/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_MAINWINDOW_H
#define TDEAICLIENT_MAINWINDOW_H

#include "types.h"

#include <tqmap.h>
#include <tqptrlist.h>

#include <tdemainwindow.h>

class Backend;
class ChatView;
class InputEdit;
class KPushButton;
class TDESelectAction;
class TDEAction;
class TDEListBox;
class TDERadioAction;
class TQHBox;
class TQImage;
class TQLabel;
class TQListBoxItem;
class TQSplitter;
class TQTimer;

class MainWindow : public TDEMainWindow
{
    TQ_OBJECT

public:
    MainWindow();
    ~MainWindow();

    /** Switches to the service with this id ("horde", "koboldcpp", "openai"). */
    bool selectBackend( const TQString &id );
    /** "chat", "image" or "speech". */
    bool selectMode( const TQString &mode );
    void attachFiles( const TQStringList &paths );
    /** Puts @p text into the message editor and sends it. */
    void sendPrompt( const TQString &text );
    /** What the command line asks for; done once the service has told what it can do. */
    void startWith( const TQString &mode, const TQStringList &files, const TQString &prompt );

protected:
    bool queryClose();

private slots:
    void slotNewChat();
    void slotAttach();
    void slotClearAttachments();
    void slotRemoveAttachment( int index );
    void slotImagePasted( const TQImage &image );
    void slotFilesDropped( const TQStringList &paths );
    void slotSend();
    void slotStop();
    void slotRegenerate();
    void slotExport();
    void slotRename();
    void slotDelete();
    void slotCopy();
    void slotConversationSelected( int index );
    void slotListContextMenu( TQListBoxItem *item, const TQPoint &pos );
    void slotBackendCombo( int index );
    void slotBackendRadio();
    void slotModeCombo( int index );
    void slotModeRadio();
    void slotModelChosen( const TQString &model );
    void slotRefresh();
    void slotPreferences();
    void slotSettingsChanged();
    void slotOpenFile( const TQString &path );
    void slotSaveFile( const TQString &path );
    void slotRender();
    void slotUpdateAttachmentBar() { updateAttachmentBar(); }

    /* backend */
    void slotTextDelta( const TQString &text );
    void slotMedia( const Attachment &file );
    void slotMeta( const TQString &text );
    void slotStatus( const TQString &text );
    void slotNote( const TQString &text );
    void slotServiceMessage( const TQString &text );
    void slotFinished();
    void slotFailed( const TQString &message );
    void slotModelsChanged();
    void slotCapabilitiesChanged();
    void slotAccountInfo( const TQString &text );

private:
    void setupWidgets();
    void setupActions();
    void setBackend( Backend *backend );
    void setMode( Request::Mode mode );
    void updateModes();
    void updateModels();
    void updateActions();
    void updateAttachmentBar();
    void updateServiceLabel();
    void rebuildList();
    void render();
    void scheduleRender();
    void setStatus( const TQString &text );
    void startRequest( const Request &request );
    void requestEnded();
    Conversation &current();
    ChatMessage *answer();
    TQString welcomeText() const;
    TQString currentModel() const;
    void runStartRequest();

    TQPtrList<Backend>        m_backends;
    Backend                  *m_backend;
    Request::Mode             m_mode;
    TQValueList<int>          m_comboModes;
    TQValueList<Conversation> m_conversations;
    int                       m_current;
    AttachmentList            m_pending;
    TQString                  m_pendingStatus;
    TQMap<TQString, TQString> m_account;
    bool                      m_updating;
    bool                      m_startPending;
    TQString                  m_startMode;
    TQStringList              m_startFiles;
    TQString                  m_startPrompt;

    TQSplitter  *m_splitter;
    TDEListBox  *m_list;
    ChatView    *m_view;
    TQHBox      *m_attachBar;
    InputEdit   *m_input;
    KPushButton *m_sendButton;
    KPushButton *m_stopButton;
    KPushButton *m_attachButton;
    TQLabel     *m_statusLabel;
    TQLabel     *m_serviceLabel;
    TQTimer     *m_renderTimer;

    TDESelectAction  *m_backendSelect;
    TDESelectAction  *m_modeSelect;
    TDESelectAction  *m_modelSelect;
    TDERadioAction *m_backendRadio[3];
    TDERadioAction *m_modeRadio[Request::ModeCount];
    TDEAction      *m_sendAction;
    TDEAction      *m_stopAction;
    TDEAction      *m_regenerateAction;
    TDEAction      *m_exportAction;
    TDEAction      *m_renameAction;
    TDEAction      *m_deleteAction;
    TDEAction      *m_clearAttachAction;
};

#endif
