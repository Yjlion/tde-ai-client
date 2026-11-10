/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_INPUTEDIT_H
#define TDEAICLIENT_INPUTEDIT_H

#include <tqimage.h>
#include <tqstringlist.h>

#include <ktextedit.h>

/** The message editor: Enter sends, files and pictures can be dropped or pasted. */
class InputEdit : public KTextEdit
{
    TQ_OBJECT

public:
    InputEdit( TQWidget *parent, const char *name = 0 );

    void setSendOnEnter( bool on ) { m_sendOnEnter = on; }

public slots:
    void paste();

signals:
    void sendRequested();
    void filesDropped( const TQStringList &paths );
    void imagePasted( const TQImage &image );

protected:
    void keyPressEvent( TQKeyEvent *e );
    void contentsDragEnterEvent( TQDragEnterEvent *e );
    void contentsDragMoveEvent( TQDragMoveEvent *e );
    void contentsDropEvent( TQDropEvent *e );

private:
    bool m_sendOnEnter;
};

#endif
