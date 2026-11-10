/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "mainwindow.h"
#include "backend.h"
#include "chatview.h"
#include "conversationstore.h"
#include "hordebackend.h"
#include "inputedit.h"
#include "koboldbackend.h"
#include "openaibackend.h"
#include "settings.h"
#include "settingsdialog.h"

#include <tqfile.h>
#include <tqfileinfo.h>
#include <tqhbox.h>
#include <tqimage.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqobjectlist.h>
#include <tqsignalmapper.h>
#include <tqsplitter.h>
#include <tqtimer.h>
#include <tqtoolbutton.h>
#include <tqtooltip.h>
#include <tqvbox.h>

#include <tdeaction.h>
#include <tdeapplication.h>
#include <tdeconfig.h>
#include <tdefiledialog.h>
#include <tdeglobal.h>
#include <tdeio/netaccess.h>
#include <kiconloader.h>
#include <kinputdialog.h>
#include <tdelistbox.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <kmimetype.h>
#include <tdepopupmenu.h>
#include <kpushbutton.h>
#include <krun.h>
#include <kstatusbar.h>
#include <kstdaction.h>
#include <kurl.h>

static const char *const modeIds[Request::ModeCount] = { "chat", "image", "speech" };

static TQString modeName( int mode )
{
    switch ( mode ) {
    case Request::Image:  return i18n( "Generate Image" );
    case Request::Speech: return i18n( "Text to Speech" );
    default:              return i18n( "Chat" );
    }
}

MainWindow::MainWindow()
    : TDEMainWindow( 0, "tde-ai-client-mainwindow" ),
      m_backend( 0 ), m_mode( Request::Chat ), m_current( -1 ), m_updating( false ), m_startPending( false )
{
    m_backends.append( new HordeBackend( this ) );
    m_backends.append( new KoboldBackend( this ) );
    m_backends.append( new OpenAIBackend( this ) );
    for ( Backend *b = m_backends.first(); b; b = m_backends.next() ) {
        connect( b, TQ_SIGNAL( textDelta( const TQString & ) ), this, TQ_SLOT( slotTextDelta( const TQString & ) ) );
        connect( b, TQ_SIGNAL( media( const Attachment & ) ), this, TQ_SLOT( slotMedia( const Attachment & ) ) );
        connect( b, TQ_SIGNAL( meta( const TQString & ) ), this, TQ_SLOT( slotMeta( const TQString & ) ) );
        connect( b, TQ_SIGNAL( status( const TQString & ) ), this, TQ_SLOT( slotStatus( const TQString & ) ) );
        connect( b, TQ_SIGNAL( note( const TQString & ) ), this, TQ_SLOT( slotNote( const TQString & ) ) );
        connect( b, TQ_SIGNAL( serviceMessage( const TQString & ) ), this, TQ_SLOT( slotServiceMessage( const TQString & ) ) );
        connect( b, TQ_SIGNAL( finished() ), this, TQ_SLOT( slotFinished() ) );
        connect( b, TQ_SIGNAL( failed( const TQString & ) ), this, TQ_SLOT( slotFailed( const TQString & ) ) );
        connect( b, TQ_SIGNAL( modelsChanged() ), this, TQ_SLOT( slotModelsChanged() ) );
        connect( b, TQ_SIGNAL( capabilitiesChanged() ), this, TQ_SLOT( slotCapabilitiesChanged() ) );
        connect( b, TQ_SIGNAL( accountInfo( const TQString & ) ), this, TQ_SLOT( slotAccountInfo( const TQString & ) ) );
    }

    m_renderTimer = new TQTimer( this );
    connect( m_renderTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotRender() ) );

    setupWidgets();
    setupActions();

    m_statusLabel = new TQLabel( statusBar() );
    m_statusLabel->setSizePolicy( TQSizePolicy( TQSizePolicy::Ignored, TQSizePolicy::Preferred ) );
    m_statusLabel->setMinimumWidth( 1 );
    statusBar()->addWidget( m_statusLabel, 1 );
    m_serviceLabel = new TQLabel( statusBar() );
    statusBar()->addWidget( m_serviceLabel, 0, true );

    setupGUI( ToolBar | Keys | StatusBar | Save | Create );

    TDEConfig *c = TDEGlobal::config();
    c->setGroup( "Layout" );
    TQValueList<int> sizes = c->readIntListEntry( "Splitter" );
    if ( sizes.count() != 2 )
        sizes << 190 << 610;
    m_splitter->setSizes( sizes );
    if ( !c->hasGroup( "MainWindow" ) )
        resize( 860, 620 );

    m_conversations = ConversationStore::loadAll();
    slotNewChat();

    const Settings &s = Settings::self();
    m_input->setSendOnEnter( s.sendOnEnter );
    m_mode = ( s.lastMode >= 0 && s.lastMode < Request::ModeCount ) ? (Request::Mode) s.lastMode : Request::Chat;
    if ( !selectBackend( s.lastBackend ) )
        setBackend( m_backends.first() );

    setStatus( i18n( "Ready" ) );
    m_input->setFocus();
}

MainWindow::~MainWindow()
{
}

/* ----------------------------------------------------------------- setup */

void MainWindow::setupWidgets()
{
    m_splitter = new TQSplitter( TQt::Horizontal, this, "main splitter" );

    m_list = new TDEListBox( m_splitter, "conversations" );
    m_list->setHScrollBarMode( TQScrollView::AlwaysOff );
    connect( m_list, TQ_SIGNAL( highlighted( int ) ), this, TQ_SLOT( slotConversationSelected( int ) ) );
    connect( m_list, TQ_SIGNAL( contextMenuRequested( TQListBoxItem *, const TQPoint & ) ),
             this, TQ_SLOT( slotListContextMenu( TQListBoxItem *, const TQPoint & ) ) );

    TQWidget *right = new TQWidget( m_splitter );
    TQVBoxLayout *rl = new TQVBoxLayout( right, 0, 4 );

    m_view = new ChatView( right, "chat view" );
    connect( m_view, TQ_SIGNAL( openFile( const TQString & ) ), this, TQ_SLOT( slotOpenFile( const TQString & ) ) );
    connect( m_view, TQ_SIGNAL( saveFile( const TQString & ) ), this, TQ_SLOT( slotSaveFile( const TQString & ) ) );
    connect( m_view, TQ_SIGNAL( filesDropped( const TQStringList & ) ), this, TQ_SLOT( slotFilesDropped( const TQStringList & ) ) );
    rl->addWidget( m_view, 1 );

    m_attachBar = new TQHBox( right );
    m_attachBar->setSpacing( 4 );
    m_attachBar->hide();
    rl->addWidget( m_attachBar );

    TQHBoxLayout *il = new TQHBoxLayout( rl, 4 );
    m_input = new InputEdit( right, "message editor" );
    m_input->setMinimumHeight( 54 );
    m_input->setMaximumHeight( 110 );
    connect( m_input, TQ_SIGNAL( sendRequested() ), this, TQ_SLOT( slotSend() ) );
    connect( m_input, TQ_SIGNAL( filesDropped( const TQStringList & ) ), this, TQ_SLOT( slotFilesDropped( const TQStringList & ) ) );
    connect( m_input, TQ_SIGNAL( imagePasted( const TQImage & ) ), this, TQ_SLOT( slotImagePasted( const TQImage & ) ) );
    il->addWidget( m_input, 1 );

    TQVBoxLayout *bl = new TQVBoxLayout( il, 4 );
    m_sendButton = new KPushButton( SmallIconSet( "mail-send" ), i18n( "&Send" ), right );
    connect( m_sendButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotSend() ) );
    bl->addWidget( m_sendButton );
    m_stopButton = new KPushButton( SmallIconSet( "process-stop" ), i18n( "S&top" ), right );
    connect( m_stopButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotStop() ) );
    bl->addWidget( m_stopButton );
    m_attachButton = new KPushButton( SmallIconSet( "attach" ), i18n( "&Attach..." ), right );
    connect( m_attachButton, TQ_SIGNAL( clicked() ), this, TQ_SLOT( slotAttach() ) );
    bl->addWidget( m_attachButton );
    bl->addStretch( 1 );

    m_splitter->setResizeMode( m_list, TQSplitter::KeepSize );
    setCentralWidget( m_splitter );
}

void MainWindow::setupActions()
{
    TDEActionCollection *ac = actionCollection();

    TDEAction *a = KStdAction::openNew( this, TQ_SLOT( slotNewChat() ), ac );
    a->setText( i18n( "&New Chat" ) );
    KStdAction::quit( this, TQ_SLOT( close() ), ac );
    KStdAction::copy( this, TQ_SLOT( slotCopy() ), ac );
    KStdAction::preferences( this, TQ_SLOT( slotPreferences() ), ac );

    m_exportAction = new TDEAction( i18n( "&Export Chat..." ), "document-save-as", CTRL + Key_S,
                                    this, TQ_SLOT( slotExport() ), ac, "conversation_export" );
    m_renameAction = new TDEAction( i18n( "&Rename Chat..." ), "edit", Key_F2,
                                    this, TQ_SLOT( slotRename() ), ac, "conversation_rename" );
    m_deleteAction = new TDEAction( i18n( "&Delete Chat" ), "edit-delete", CTRL + Key_D,
                                    this, TQ_SLOT( slotDelete() ), ac, "conversation_delete" );

    m_sendAction = new TDEAction( i18n( "&Send" ), "mail-send", CTRL + Key_Return,
                                  this, TQ_SLOT( slotSend() ), ac, "chat_send" );
    m_stopAction = new TDEAction( i18n( "S&top" ), "process-stop", Key_Escape,
                                  this, TQ_SLOT( slotStop() ), ac, "chat_stop" );
    m_regenerateAction = new TDEAction( i18n( "Answer &Again" ), "reload", CTRL + Key_R,
                                        this, TQ_SLOT( slotRegenerate() ), ac, "chat_regenerate" );
    new TDEAction( i18n( "&Attach Files..." ), "attach", CTRL + Key_O,
                   this, TQ_SLOT( slotAttach() ), ac, "chat_attach" );
    m_clearAttachAction = new TDEAction( i18n( "&Remove All Attachments" ), "edit-clear", 0,
                                         this, TQ_SLOT( slotClearAttachments() ), ac, "chat_clear_attachments" );

    /* services: radio items in the menu, a combo box in the toolbar */
    TQStringList names;
    int i = 0;
    for ( Backend *b = m_backends.first(); b; b = m_backends.next(), ++i ) {
        names << b->name();
        m_backendRadio[i] = new TDERadioAction( b->name(), 0, this, TQ_SLOT( slotBackendRadio() ), ac,
                                                TQCString( "backend_" ) + b->id().latin1() );
        m_backendRadio[i]->setExclusiveGroup( "backend" );
    }
    m_backendSelect = new TDESelectAction( i18n( "Service" ), 0, ac, "backend_select" );
    m_backendSelect->setItems( names );
    m_backendSelect->setComboWidth( 180 );
    m_backendSelect->setToolTip( i18n( "The AI service that answers" ) );
    connect( m_backendSelect, TQ_SIGNAL( activated( int ) ), this, TQ_SLOT( slotBackendCombo( int ) ) );

    for ( i = 0; i < Request::ModeCount; ++i ) {
        m_modeRadio[i] = new TDERadioAction( modeName( i ), 0, this, TQ_SLOT( slotModeRadio() ), ac,
                                             TQCString( "mode_" ) + modeIds[i] );
        m_modeRadio[i]->setExclusiveGroup( "mode" );
    }
    m_modeSelect = new TDESelectAction( i18n( "Task" ), 0, ac, "mode_select" );
    m_modeSelect->setComboWidth( 140 );
    m_modeSelect->setToolTip( i18n( "What the service should do with your message" ) );
    connect( m_modeSelect, TQ_SIGNAL( activated( int ) ), this, TQ_SLOT( slotModeCombo( int ) ) );

    m_modelSelect = new TDESelectAction( i18n( "Model" ), 0, ac, "model_select" );
    m_modelSelect->setEditable( true );
    m_modelSelect->setComboWidth( 260 );
    m_modelSelect->setToolTip( i18n( "The model to use; you can also type a name" ) );
    connect( m_modelSelect, TQ_SIGNAL( activated( const TQString & ) ), this, TQ_SLOT( slotModelChosen( const TQString & ) ) );

    new TDEAction( i18n( "&Reload Models" ), "reload", Key_F5,
                   this, TQ_SLOT( slotRefresh() ), ac, "service_refresh" );
}

/* ------------------------------------------------------- service & mode */

bool MainWindow::selectBackend( const TQString &id )
{
    for ( Backend *b = m_backends.first(); b; b = m_backends.next() ) {
        if ( b->id() == id.lower() ) {
            setBackend( b );
            return true;
        }
    }
    return false;
}

bool MainWindow::selectMode( const TQString &mode )
{
    for ( int i = 0; i < Request::ModeCount; ++i ) {
        if ( mode.lower() == modeIds[i] ) {
            setMode( (Request::Mode) i );
            return m_mode == i;
        }
    }
    setStatus( i18n( "Unknown task \"%1\". Known are: chat, image, speech." ).arg( mode ) );
    return false;
}

void MainWindow::setBackend( Backend *backend )
{
    if ( !backend || ( m_backend && m_backend->busy() ) )
        return;
    const bool changed = backend != m_backend;
    m_backend = backend;
    Settings::self().lastBackend = backend->id();

    const int index = m_backends.findRef( backend );
    m_updating = true;
    m_backendSelect->setCurrentItem( index );
    m_backendRadio[index]->setChecked( true );
    m_updating = false;

    updateModes();
    updateServiceLabel();
    updateAttachmentBar();
    render();
    if ( changed ) {
        Settings::self().save();    // remembered even if the session ends abruptly
        backend->refresh();
    }
}

void MainWindow::setMode( Request::Mode mode )
{
    if ( m_backend && !m_backend->supports( mode ) ) {
        setStatus( i18n( "%1 does not offer \"%2\"." ).arg( m_backend->name() ).arg( modeName( mode ) ) );
        mode = Request::Chat;
    }
    m_mode = mode;
    Settings::self().lastMode = mode;
    updateModes();
    updateAttachmentBar();
}

void MainWindow::updateModes()
{
    if ( !m_backend )
        return;
    if ( !m_backend->supports( m_mode ) )
        m_mode = Request::Chat;

    m_updating = true;
    TQStringList names;
    m_comboModes.clear();
    for ( int i = 0; i < Request::ModeCount; ++i ) {
        const bool ok = m_backend->supports( (Request::Mode) i );
        m_modeRadio[i]->setEnabled( ok );
        m_modeRadio[i]->setChecked( i == m_mode );
        if ( ok ) {
            m_comboModes << i;
            names << modeName( i );
        }
    }
    m_modeSelect->setItems( names );
    m_modeSelect->setCurrentItem( m_comboModes.findIndex( m_mode ) );
    m_updating = false;

    switch ( m_mode ) {
    case Request::Image:  m_sendButton->setText( i18n( "&Generate" ) ); break;
    case Request::Speech: m_sendButton->setText( i18n( "&Speak" ) ); break;
    default:              m_sendButton->setText( i18n( "&Send" ) );
    }
    updateModels();
    updateActions();
}

void MainWindow::updateModels()
{
    if ( !m_backend )
        return;
    const TQString chosen = currentModel();
    TQStringList items;
    items << i18n( "(Default model)" );
    items += m_backend->models( m_mode );
    if ( !chosen.isEmpty() && !items.contains( chosen ) )
        items << chosen;

    m_updating = true;
    m_modelSelect->setItems( items );
    m_modelSelect->setCurrentItem( chosen.isEmpty() ? 0 : items.findIndex( chosen ) );
    m_updating = false;
}

TQString MainWindow::currentModel() const
{
    return m_backend ? Settings::self().model( m_backend->id(), m_mode ) : TQString();
}

void MainWindow::slotBackendCombo( int index )
{
    if ( m_updating )
        return;
    if ( m_backend && m_backend->busy() ) {     // not while an answer is on its way
        m_updating = true;
        m_backendSelect->setCurrentItem( m_backends.findRef( m_backend ) );
        m_updating = false;
        return;
    }
    setBackend( m_backends.at( index ) );
}

void MainWindow::slotBackendRadio()
{
    if ( m_updating )
        return;
    for ( uint i = 0; i < m_backends.count(); ++i ) {
        if ( m_backendRadio[i]->isChecked() && m_backends.at( i ) != m_backend ) {
            if ( m_backend && m_backend->busy() ) {
                m_updating = true;
                m_backendRadio[m_backends.findRef( m_backend )]->setChecked( true );
                m_updating = false;
            } else {
                setBackend( m_backends.at( i ) );
            }
            return;
        }
    }
}

void MainWindow::slotModeCombo( int index )
{
    if ( m_updating || index < 0 || index >= (int) m_comboModes.count() )
        return;
    setMode( (Request::Mode) m_comboModes[index] );
}

void MainWindow::slotModeRadio()
{
    if ( m_updating )
        return;
    for ( int i = 0; i < Request::ModeCount; ++i ) {
        if ( m_modeRadio[i]->isChecked() && i != m_mode ) {
            setMode( (Request::Mode) i );
            return;
        }
    }
}

void MainWindow::slotModelChosen( const TQString &model )
{
    if ( m_updating || !m_backend )
        return;
    TQString m = model.stripWhiteSpace();
    if ( m == i18n( "(Default model)" ) )
        m = TQString();
    Settings::self().setModel( m_backend->id(), m_mode, m );
    updateModels();
    m_input->setFocus();
}

void MainWindow::slotRefresh()
{
    if ( !m_backend )
        return;
    setStatus( i18n( "Asking %1 for its models..." ).arg( m_backend->name() ) );
    m_backend->refresh();
}

void MainWindow::slotModelsChanged()
{
    if ( sender() != m_backend )
        return;
    updateModels();
    if ( !m_backend->busy() ) {
        const int n = m_backend->models( m_mode ).count();
        setStatus( n ? i18n( "%1 offers one model.", "%1 offers %n models.", n ).arg( m_backend->name() )
                     : i18n( "Ready" ) );
    }
}

void MainWindow::slotCapabilitiesChanged()
{
    if ( sender() != m_backend )
        return;
    updateModes();
    updateAttachmentBar();
    if ( m_startPending )
        runStartRequest();
}

void MainWindow::startWith( const TQString &mode, const TQStringList &files, const TQString &prompt )
{
    m_startMode = mode;
    m_startFiles = files;
    m_startPrompt = prompt;
    m_startPending = true;
    if ( m_backend && m_backend->ready() )
        runStartRequest();
}

void MainWindow::runStartRequest()
{
    m_startPending = false;
    if ( !m_startMode.isEmpty() && !selectMode( m_startMode ) )
        return;     // do not send the prompt to a task that was not asked for
    if ( !m_startFiles.isEmpty() )
        attachFiles( m_startFiles );
    if ( !m_startPrompt.isEmpty() )
        sendPrompt( m_startPrompt );
}

void MainWindow::slotServiceMessage( const TQString &text )
{
    if ( sender() == m_backend && !m_backend->busy() )
        setStatus( text );
}

void MainWindow::slotAccountInfo( const TQString &text )
{
    const Backend *b = static_cast<const Backend *>( sender() );
    m_account.replace( b->id(), text );
    updateServiceLabel();
}

void MainWindow::updateServiceLabel()
{
    if ( !m_backend )
        return;
    TQString text = m_backend->name() + " - " + m_backend->address();
    const TQString account = m_account[m_backend->id()];
    if ( !account.isEmpty() )
        text += " - " + account;
    m_serviceLabel->setText( " " + text + " " );
    m_serviceLabel->setMinimumWidth( m_serviceLabel->sizeHint().width() );
}

/* --------------------------------------------------------- conversations */

Conversation &MainWindow::current()
{
    return m_conversations[m_current];
}

ChatMessage *MainWindow::answer()
{
    if ( m_current < 0 || current().messages.isEmpty() )
        return 0;
    ChatMessage &last = current().messages.last();
    return last.role == ChatMessage::Assistant ? &last : 0;
}

void MainWindow::rebuildList()
{
    m_updating = true;
    m_list->clear();
    for ( TQValueList<Conversation>::ConstIterator it = m_conversations.begin(); it != m_conversations.end(); ++it )
        m_list->insertItem( SmallIcon( "mail-message-new" ),
                            ( *it ).title.isEmpty() ? i18n( "New chat" ) : ( *it ).title );
    if ( m_current >= 0 )
        m_list->setCurrentItem( m_current );
    m_updating = false;
}

void MainWindow::slotNewChat()
{
    if ( m_backend && m_backend->busy() )
        return;
    if ( m_current < 0 || !current().messages.isEmpty() ) {
        /* keep at most one empty conversation, at the top */
        if ( m_conversations.isEmpty() || !m_conversations.first().messages.isEmpty() )
            m_conversations.prepend( Conversation() );
        m_current = 0;
    }
    rebuildList();
    render();
    updateActions();
    m_input->setFocus();
}

void MainWindow::slotConversationSelected( int index )
{
    if ( m_updating || index < 0 || index == m_current )
        return;
    if ( m_backend && m_backend->busy() ) {
        m_updating = true;
        m_list->setCurrentItem( m_current );
        m_updating = false;
        return;
    }
    m_current = index;
    render();
    m_view->scrollToBottom();
    updateActions();
}

void MainWindow::slotListContextMenu( TQListBoxItem *item, const TQPoint &pos )
{
    if ( !item )
        return;
    TDEPopupMenu menu( this );
    m_renameAction->plug( &menu );
    m_exportAction->plug( &menu );
    menu.insertSeparator();
    m_deleteAction->plug( &menu );
    menu.exec( pos );
}

void MainWindow::slotRename()
{
    if ( m_current < 0 || current().messages.isEmpty() )
        return;
    bool ok = false;
    const TQString title = KInputDialog::getText( i18n( "Rename Chat" ), i18n( "Name of the chat:" ),
                                                  current().title, &ok, this ).stripWhiteSpace();
    if ( !ok || title.isEmpty() )
        return;
    current().title = title;
    const uint when = current().modified;
    ConversationStore::save( current() );
    current().modified = when;
    rebuildList();
    render();
}

void MainWindow::slotDelete()
{
    if ( m_current < 0 || ( m_backend && m_backend->busy() ) || current().messages.isEmpty() )
        return;
    if ( KMessageBox::warningContinueCancel( this,
             i18n( "Delete the chat \"%1\"? This cannot be undone." ).arg( current().title ),
             i18n( "Delete Chat" ), KStdGuiItem::del() ) != KMessageBox::Continue )
        return;
    ConversationStore::remove( current().id );
    m_conversations.remove( m_conversations.at( m_current ) );
    m_current = -1;
    if ( !m_conversations.isEmpty() && m_conversations.first().messages.isEmpty() )
        m_current = 0;
    if ( m_current < 0 )
        slotNewChat();
    else {
        rebuildList();
        render();
        updateActions();
    }
}

void MainWindow::slotExport()
{
    if ( m_current < 0 || current().messages.isEmpty() )
        return;
    const TQString path = KFileDialog::getSaveFileName( ":tde-ai-client-export",
        i18n( "*.html|Web Page (*.html)\n*.txt|Plain Text (*.txt)" ), this, i18n( "Export Chat" ) );
    if ( path.isEmpty() )
        return;
    if ( TQFile::exists( path ) && KMessageBox::warningContinueCancel( this,
             i18n( "The file %1 already exists. Overwrite it?" ).arg( path ), i18n( "Export Chat" ),
             i18n( "Overwrite" ) ) != KMessageBox::Continue )
        return;
    const bool html = !path.lower().endsWith( ".txt" );
    if ( !ConversationStore::exportTo( current(), path, html ) )
        KMessageBox::error( this, i18n( "The chat could not be written to %1." ).arg( path ) );
    else
        setStatus( i18n( "Chat exported to %1" ).arg( path ) );
}

void MainWindow::slotCopy()
{
    if ( m_input->hasFocus() && m_input->hasSelectedText() )
        m_input->copy();
    else
        m_view->copy();
}

/* ----------------------------------------------------------- attachments */

void MainWindow::slotAttach()
{
    const TQStringList files = KFileDialog::getOpenFileNames( ":tde-ai-client-attach", TQString(), this,
                                                             i18n( "Attach Files" ) );
    attachFiles( files );
}

void MainWindow::attachFiles( const TQStringList &paths )
{
    for ( TQStringList::ConstIterator it = paths.begin(); it != paths.end(); ++it ) {
        TQFileInfo info( *it );
        if ( !info.isFile() || !info.isReadable() ) {
            setStatus( i18n( "%1 cannot be read." ).arg( *it ) );
            continue;
        }
        bool have = false;
        for ( AttachmentList::ConstIterator a = m_pending.begin(); a != m_pending.end(); ++a )
            have = have || ( *a ).path == info.absFilePath();
        if ( !have )
            m_pending.append( Attachment::fromFile( info.absFilePath() ) );
    }
    updateAttachmentBar();
    m_input->setFocus();
}

void MainWindow::slotFilesDropped( const TQStringList &paths )
{
    attachFiles( paths );
}

void MainWindow::slotImagePasted( const TQImage &image )
{
    const TQString path = ConversationStore::newMediaPath( "png" );
    if ( image.save( path, "PNG" ) )
        attachFiles( TQStringList( path ) );
}

void MainWindow::slotClearAttachments()
{
    m_pending.clear();
    updateAttachmentBar();
}

void MainWindow::slotRemoveAttachment( int index )
{
    if ( index < 0 || index >= (int) m_pending.count() )
        return;
    m_pending.remove( m_pending.at( index ) );
    /* the button that was clicked is deleted in there: do it after its signal */
    TQTimer::singleShot( 0, this, TQ_SLOT( slotUpdateAttachmentBar() ) );
}

void MainWindow::updateAttachmentBar()
{
    if ( !m_backend )
        return;
    const TQObjectList *old = m_attachBar->children();
    if ( old ) {
        TQObjectList copy = *old;
        for ( TQObject *o = copy.first(); o; o = copy.next() )
            if ( o->isWidgetType() || o->inherits( "TQSignalMapper" ) )
                delete o;
    }
    m_clearAttachAction->setEnabled( !m_pending.isEmpty() );
    if ( m_pending.isEmpty() ) {
        m_attachBar->hide();
        return;
    }

    TQSignalMapper *mapper = new TQSignalMapper( m_attachBar );
    connect( mapper, TQ_SIGNAL( mapped( int ) ), this, TQ_SLOT( slotRemoveAttachment( int ) ) );

    int index = 0;
    for ( AttachmentList::ConstIterator a = m_pending.begin(); a != m_pending.end(); ++a, ++index ) {
        const bool ok = m_backend->accepts( m_mode, ( *a ).kind );
        TQToolButton *chip = new TQToolButton( m_attachBar );
        chip->setUsesTextLabel( true );
        chip->setTextPosition( TQToolButton::BesideIcon );
        chip->setAutoRaise( true );
        chip->setIconSet( SmallIconSet( ok ? KMimeType::mimeType( ( *a ).mime )->icon( TQString(), true )
                                           : TQString( "messagebox_warning" ) ) );
        chip->setTextLabel( ( *a ).name(), false );
        TQString tip = i18n( "%1 (%2)\nClick to remove." ).arg( ( *a ).path ).arg( Attachment::kindName( ( *a ).kind ) );
        if ( !ok )
            tip = i18n( "%1 cannot take this kind of file (%2) for this task; it will not be sent.\n" )
                  .arg( m_backend->name() ).arg( Attachment::kindName( ( *a ).kind ) ) + tip;
        TQToolTip::add( chip, tip );
        mapper->setMapping( chip, index );
        connect( chip, TQ_SIGNAL( clicked() ), mapper, TQ_SLOT( map() ) );
        chip->show();
    }
    TQWidget *filler = new TQWidget( m_attachBar );
    m_attachBar->setStretchFactor( filler, 1 );
    filler->show();
    m_attachBar->show();
}

/* --------------------------------------------------------------- sending */

TQString MainWindow::welcomeText() const
{
    if ( !m_backend )
        return TQString();
    TQString text = "<b>" + i18n( "Connected service: %1" ).arg( m_backend->name() ) + "</b><br><br>";
    if ( m_backend->id() == "horde" )
        text += i18n( "The AI Horde is free and run by volunteers. Requests wait in a queue, "
                      "so an answer can take a while." );
    else if ( m_backend->id() == "koboldcpp" )
        text += i18n( "KoboldCpp runs on your own computer or network. Set its address in "
                      "Settings - Configure TDE AI Client." );
    else
        text += i18n( "Any service with an OpenAI-compatible API. Set its address and your API key in "
                      "Settings - Configure TDE AI Client." );
    text += "<br><br>" + i18n( "Type a message below and press Enter. Files can be attached with the "
                               "Attach button, by dropping them on this window, or by pasting." );
    return text;
}

void MainWindow::render()
{
    m_renderTimer->stop();
    if ( m_current < 0 )
        return;
    const bool busy = m_backend && m_backend->busy();
    m_view->showMessages( current().messages, busy ? m_pendingStatus : TQString(), welcomeText() );
    const TQString title = current().title;
    setCaption( title.isEmpty() ? TQString() : title );
}

void MainWindow::slotRender()
{
    render();
}

void MainWindow::scheduleRender()
{
    if ( !m_renderTimer->isActive() )
        m_renderTimer->start( 120, true );
}

void MainWindow::setStatus( const TQString &text )
{
    m_statusLabel->setText( " " + text );
}

void MainWindow::updateActions()
{
    const bool busy = m_backend && m_backend->busy();
    const bool hasChat = m_current >= 0 && !current().messages.isEmpty();

    bool canRegenerate = false;
    if ( hasChat && !busy ) {
        for ( MessageList::ConstIterator m = current().messages.begin(); m != current().messages.end(); ++m )
            canRegenerate = canRegenerate || ( *m ).role == ChatMessage::User;
    }

    m_sendAction->setEnabled( !busy );
    m_sendButton->setEnabled( !busy );
    m_stopAction->setEnabled( busy );
    m_stopButton->setEnabled( busy );
    m_regenerateAction->setEnabled( canRegenerate );
    m_exportAction->setEnabled( hasChat );
    m_renameAction->setEnabled( hasChat && !busy );
    m_deleteAction->setEnabled( hasChat && !busy );
    m_list->setEnabled( !busy );
    m_backendSelect->setEnabled( !busy );
    m_modeSelect->setEnabled( !busy );
    m_modelSelect->setEnabled( !busy );
    for ( uint i = 0; i < m_backends.count(); ++i )
        m_backendRadio[i]->setEnabled( !busy );
    actionCollection()->action( "file_new" )->setEnabled( !busy );
}

void MainWindow::sendPrompt( const TQString &text )
{
    m_input->setText( text );
    slotSend();
}

void MainWindow::slotSend()
{
    if ( !m_backend || m_backend->busy() || m_current < 0 )
        return;
    const TQString text = m_input->text().stripWhiteSpace();
    if ( text.isEmpty() && ( m_mode != Request::Chat || m_pending.isEmpty() ) ) {
        setStatus( m_mode == Request::Image ? i18n( "Describe the image you want first." )
                   : m_mode == Request::Speech ? i18n( "Type the text to be spoken first." )
                   : i18n( "Type a message first." ) );
        return;
    }

    AttachmentList send;
    TQStringList skipped;
    for ( AttachmentList::ConstIterator a = m_pending.begin(); a != m_pending.end(); ++a ) {
        if ( m_backend->accepts( m_mode, ( *a ).kind ) )
            send.append( *a );
        else
            skipped << ( *a ).name();
    }
    if ( !skipped.isEmpty() ) {
        if ( KMessageBox::warningContinueCancelList( this,
                 i18n( "%1 cannot take these files for the task \"%2\". They will not be sent:" )
                 .arg( m_backend->name() ).arg( modeName( m_mode ) ),
                 skipped, i18n( "Attachments" ), i18n( "Send Without Them" ) ) != KMessageBox::Continue )
            return;
        if ( text.isEmpty() && send.isEmpty() )
            return;
    }

    Conversation &c = current();
    if ( c.title.isEmpty() ) {
        TQString title = text.isEmpty() ? send.first().name() : text.simplifyWhiteSpace();
        if ( title.length() > 48 )
            title = title.left( 46 ) + "...";
        c.title = title;
    }
    c.backend = m_backend->id();

    ChatMessage user( ChatMessage::User, text );
    user.attachments = send;
    c.messages.append( user );

    Request request;
    request.mode = m_mode;
    request.model = currentModel();
    if ( m_mode == Request::Chat ) {
        request.history = c.messages;
    } else {
        request.prompt = text;
        request.attachments = send;
    }

    m_input->clear();
    m_pending.clear();
    updateAttachmentBar();
    startRequest( request );
}

void MainWindow::startRequest( const Request &request )
{
    current().messages.append( ChatMessage( ChatMessage::Assistant, TQString() ) );
    m_pendingStatus = i18n( "Waiting for %1..." ).arg( m_backend->name() );
    setStatus( m_pendingStatus );
    m_backend->start( request );
    rebuildList();
    updateActions();
    render();
    m_view->scrollToBottom();
}

void MainWindow::slotRegenerate()
{
    if ( !m_backend || m_backend->busy() || m_current < 0 )
        return;
    MessageList &msgs = current().messages;
    while ( !msgs.isEmpty() && msgs.last().role != ChatMessage::User )
        msgs.remove( msgs.fromLast() );
    if ( msgs.isEmpty() ) {
        render();
        updateActions();
        return;
    }
    const ChatMessage &user = msgs.last();
    Request request;
    request.mode = m_mode;
    request.model = currentModel();
    if ( m_mode == Request::Chat ) {
        request.history = msgs;
    } else {
        if ( user.text.stripWhiteSpace().isEmpty() ) {
            setStatus( i18n( "The last message has no text to work on." ) );
            render();
            return;
        }
        request.prompt = user.text;
        for ( AttachmentList::ConstIterator a = user.attachments.begin(); a != user.attachments.end(); ++a )
            if ( m_backend->accepts( m_mode, ( *a ).kind ) )
                request.attachments.append( *a );
    }
    startRequest( request );
}

void MainWindow::slotStop()
{
    if ( !m_backend || !m_backend->busy() )
        return;
    m_backend->cancel();
    ChatMessage *a = answer();
    if ( a && a->text.isEmpty() && a->attachments.isEmpty() )
        current().messages.remove( current().messages.fromLast() );
    current().messages.append( ChatMessage( ChatMessage::Info, i18n( "Stopped." ) ) );
    setStatus( i18n( "Stopped" ) );
    requestEnded();
}

/* Saves the conversation and moves it to the top of the list. */
void MainWindow::requestEnded()
{
    m_pendingStatus = TQString();
    ConversationStore::save( current() );
    if ( m_current > 0 ) {
        Conversation c = current();
        m_conversations.remove( m_conversations.at( m_current ) );
        m_conversations.prepend( c );
        m_current = 0;
    }
    rebuildList();
    updateActions();
    render();
    m_input->setFocus();
}

void MainWindow::slotTextDelta( const TQString &text )
{
    ChatMessage *a = answer();
    if ( !a || sender() != m_backend )
        return;
    a->text += text;
    scheduleRender();
}

void MainWindow::slotMedia( const Attachment &file )
{
    ChatMessage *a = answer();
    if ( !a || sender() != m_backend )
        return;
    a->attachments.append( file );
    scheduleRender();
}

void MainWindow::slotMeta( const TQString &text )
{
    ChatMessage *a = answer();
    if ( a && sender() == m_backend && !text.isEmpty() )
        a->meta = text;
}

void MainWindow::slotStatus( const TQString &text )
{
    if ( sender() != m_backend )
        return;
    setStatus( text );
    if ( m_backend->busy() ) {
        m_pendingStatus = text;
        scheduleRender();
    }
}

void MainWindow::slotNote( const TQString &text )
{
    if ( sender() != m_backend )
        return;
    if ( !m_backend->busy() || m_current < 0 ) {
        setStatus( text );
        return;
    }
    /* above the answer that is being written */
    MessageList &msgs = current().messages;
    if ( answer() )
        msgs.insert( msgs.fromLast(), ChatMessage( ChatMessage::Info, text ) );
    else
        msgs.append( ChatMessage( ChatMessage::Info, text ) );
    scheduleRender();
}

void MainWindow::slotFinished()
{
    if ( sender() != m_backend || m_current < 0 )
        return;
    ChatMessage *a = answer();
    if ( a ) {
        a->text = a->text.stripWhiteSpace();
        if ( a->meta.isEmpty() )
            a->meta = currentModel();
        if ( a->text.isEmpty() && a->attachments.isEmpty() )
            current().messages.remove( current().messages.fromLast() );
    }
    setStatus( i18n( "Ready" ) );
    requestEnded();
    if ( m_backend->id() == "horde" )
        m_backend->refresh();   // kudos have changed
}

void MainWindow::slotFailed( const TQString &message )
{
    if ( sender() != m_backend || m_current < 0 )
        return;
    ChatMessage *a = answer();
    if ( a && a->text.isEmpty() && a->attachments.isEmpty() )
        current().messages.remove( current().messages.fromLast() );
    current().messages.append( ChatMessage( ChatMessage::Error, message ) );
    setStatus( i18n( "The request failed" ) );
    requestEnded();
}

/* --------------------------------------------------------------- various */

void MainWindow::slotOpenFile( const TQString &path )
{
    if ( !TQFile::exists( path ) ) {
        KMessageBox::sorry( this, i18n( "The file %1 no longer exists." ).arg( path ) );
        return;
    }
    KURL url;
    url.setPath( path );
    new KRun( url, this, 0, true );
}

void MainWindow::slotSaveFile( const TQString &path )
{
    KURL source;
    source.setPath( path );
    const KURL target = KFileDialog::getSaveURL( ":tde-ai-client-save/" + source.fileName(), TQString(), this,
                                                 i18n( "Save As" ) );
    if ( target.isEmpty() )
        return;
    if ( TDEIO::NetAccess::exists( target, false, this )
         && KMessageBox::warningContinueCancel( this, i18n( "The file %1 already exists. Overwrite it?" )
                .arg( target.prettyURL() ), i18n( "Save As" ), i18n( "Overwrite" ) ) != KMessageBox::Continue )
        return;
    if ( !TDEIO::NetAccess::file_copy( source, target, -1, true, false, this ) )
        KMessageBox::error( this, TDEIO::NetAccess::lastErrorString() );
    else
        setStatus( i18n( "Saved as %1" ).arg( target.prettyURL() ) );
}

void MainWindow::slotPreferences()
{
    SettingsDialog dlg( this );
    connect( &dlg, TQ_SIGNAL( settingsChanged() ), this, TQ_SLOT( slotSettingsChanged() ) );
    dlg.exec();
}

void MainWindow::slotSettingsChanged()
{
    m_input->setSendOnEnter( Settings::self().sendOnEnter );
    updateServiceLabel();
    render();
    if ( m_backend && !m_backend->busy() )
        m_backend->refresh();
}

bool MainWindow::queryClose()
{
    if ( m_backend && m_backend->busy() )
        slotStop();
    TDEConfig *c = TDEGlobal::config();
    c->setGroup( "Layout" );
    c->writeEntry( "Splitter", m_splitter->sizes() );
    Settings::self().save();
    return true;
}

#include "mainwindow.moc"
