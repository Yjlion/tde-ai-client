/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "inputedit.h"

#include <tqclipboard.h>
#include <tqdragobject.h>

#include <tdeapplication.h>
#include <kurl.h>
#include <kurldrag.h>

InputEdit::InputEdit( TQWidget *parent, const char *name )
    : KTextEdit( parent, name ), m_sendOnEnter( true )
{
    setTextFormat( TQt::PlainText );
    setWordWrap( TQTextEdit::WidgetWidth );
    setCheckSpellingEnabled( false );
}

void InputEdit::keyPressEvent( TQKeyEvent *e )
{
    if ( e->key() == TQt::Key_Return || e->key() == TQt::Key_Enter ) {
        const bool ctrl = e->state() & TQt::ControlButton;
        const bool shift = e->state() & TQt::ShiftButton;
        if ( ctrl || ( m_sendOnEnter && !shift ) ) {
            emit sendRequested();
            e->accept();
            return;
        }
        if ( shift ) {      // Shift+Enter: a plain line break
            TQKeyEvent plain( TQEvent::KeyPress, e->key(), e->ascii(), TQt::NoButton, e->text() );
            KTextEdit::keyPressEvent( &plain );
            return;
        }
    }
    KTextEdit::keyPressEvent( e );
}

void InputEdit::paste()
{
    TQMimeSource *data = TQApplication::clipboard()->data( TQClipboard::Clipboard );
    KURL::List urls;
    if ( data && KURLDrag::decode( data, urls ) && !urls.isEmpty() ) {
        TQStringList paths;
        for ( KURL::List::ConstIterator it = urls.begin(); it != urls.end(); ++it )
            if ( ( *it ).isLocalFile() )
                paths << ( *it ).path();
        if ( !paths.isEmpty() ) {
            emit filesDropped( paths );
            return;
        }
    }
    if ( data && !TQTextDrag::canDecode( data ) && TQImageDrag::canDecode( data ) ) {
        TQImage image;
        if ( TQImageDrag::decode( data, image ) && !image.isNull() ) {
            emit imagePasted( image );
            return;
        }
    }
    KTextEdit::paste();
}

void InputEdit::contentsDragEnterEvent( TQDragEnterEvent *e )
{
    if ( KURLDrag::canDecode( e ) )
        e->accept();
    else
        KTextEdit::contentsDragEnterEvent( e );
}

void InputEdit::contentsDragMoveEvent( TQDragMoveEvent *e )
{
    if ( KURLDrag::canDecode( e ) )
        e->accept();
    else
        KTextEdit::contentsDragMoveEvent( e );
}

void InputEdit::contentsDropEvent( TQDropEvent *e )
{
    KURL::List urls;
    if ( KURLDrag::decode( e, urls ) ) {
        TQStringList paths;
        for ( KURL::List::ConstIterator it = urls.begin(); it != urls.end(); ++it )
            if ( ( *it ).isLocalFile() )
                paths << ( *it ).path();
        if ( !paths.isEmpty() )
            emit filesDropped( paths );
        e->accept();
        return;
    }
    KTextEdit::contentsDropEvent( e );
}

#include "inputedit.moc"
