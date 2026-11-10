/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_CHATVIEW_H
#define TDEAICLIENT_CHATVIEW_H

#include "types.h"

#include <tqmap.h>
#include <tqtextbrowser.h>

/** Shows a conversation: text, inline images and links to other files. */
class ChatView : public TQTextBrowser
{
    TQ_OBJECT

public:
    ChatView( TQWidget *parent, const char *name = 0 );

    /** Displays @p messages. @p pending is shown where the answer will appear. */
    void showMessages( const MessageList &messages, const TQString &pending, const TQString &welcome );

    /** Turns the simple Markdown used by language models into rich text. */
    static TQString formatText( const TQString &text );

public slots:
    /** Links are handled through the signals below, the view never navigates. */
    void setSource( const TQString &name );

signals:
    void openFile( const TQString &path );
    void saveFile( const TQString &path );
    void filesDropped( const TQStringList &paths );

protected:
    void contentsDragEnterEvent( TQDragEnterEvent *e );
    void contentsDragMoveEvent( TQDragMoveEvent *e );
    void contentsDropEvent( TQDropEvent *e );

private slots:
    void slotLink( const TQString &link );

private:
    TQString thumbnail( const TQString &path );
    TQString attachmentHtml( const Attachment &a, bool generated );
    TQString blend( const TQColor &a, const TQColor &b, int percentOfB ) const;

    TQMap<TQString, TQString> m_thumbs;
};

#endif
