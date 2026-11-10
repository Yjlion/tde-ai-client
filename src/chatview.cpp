/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "chatview.h"

#include <tqdragobject.h>
#include <tqfileinfo.h>
#include <tqimage.h>
#include <tqregexp.h>
#include <tqscrollbar.h>
#include <tqstylesheet.h>

#include <tdeglobal.h>
#include <tdeio/global.h>
#include <kiconloader.h>
#include <tdelocale.h>
#include <kmdcodec.h>
#include <kmimetype.h>
#include <kstandarddirs.h>
#include <kurl.h>
#include <kurldrag.h>

static const int thumbSize = 320;

ChatView::ChatView( TQWidget *parent, const char *name )
    : TQTextBrowser( parent, name )
{
    setTextFormat( TQt::RichText );
    setWordWrap( TQTextEdit::WidgetWidth );
    viewport()->setAcceptDrops( true );
    connect( this, TQ_SIGNAL( linkClicked( const TQString & ) ), this, TQ_SLOT( slotLink( const TQString & ) ) );
}

void ChatView::setSource( const TQString & )
{
}

void ChatView::slotLink( const TQString &link )
{
    if ( link.startsWith( "open:" ) )
        emit openFile( KURL::decode_string( link.mid( 5 ) ) );
    else if ( link.startsWith( "save:" ) )
        emit saveFile( KURL::decode_string( link.mid( 5 ) ) );
}

void ChatView::contentsDragEnterEvent( TQDragEnterEvent *e )
{
    e->accept( KURLDrag::canDecode( e ) );
}

void ChatView::contentsDragMoveEvent( TQDragMoveEvent *e )
{
    e->accept( KURLDrag::canDecode( e ) );
}

void ChatView::contentsDropEvent( TQDropEvent *e )
{
    KURL::List urls;
    if ( !KURLDrag::decode( e, urls ) )
        return;
    TQStringList paths;
    for ( KURL::List::ConstIterator it = urls.begin(); it != urls.end(); ++it )
        if ( ( *it ).isLocalFile() )
            paths << ( *it ).path();
    if ( !paths.isEmpty() )
        emit filesDropped( paths );
    e->accept();
}

TQString ChatView::blend( const TQColor &a, const TQColor &b, int p ) const
{
    return TQColor( ( a.red() * ( 100 - p ) + b.red() * p ) / 100,
                    ( a.green() * ( 100 - p ) + b.green() * p ) / 100,
                    ( a.blue() * ( 100 - p ) + b.blue() * p ) / 100 ).name();
}

/* --------------------------------------------------------------- markdown */

static TQString inlineMarkup( const TQString &line )
{
    TQString s = TQStyleSheet::escape( line );

    TQRegExp code( "`([^`]+)`" );
    s.replace( code, "<tt>\\1</tt>" );
    TQRegExp bold( "\\*\\*([^*]+)\\*\\*" );
    s.replace( bold, "<b>\\1</b>" );
    TQRegExp italic( "(^|[\\s(])\\*([^*\\s][^*]*)\\*" );
    s.replace( italic, "\\1<i>\\2</i>" );
    return s;
}

TQString ChatView::formatText( const TQString &text )
{
    TQString html;
    const TQStringList lines = TQStringList::split( '\n', text, true );
    bool inCode = false;
    TQString codeBlock;

    for ( TQStringList::ConstIterator it = lines.begin(); it != lines.end(); ++it ) {
        const TQString &line = *it;
        if ( line.stripWhiteSpace().startsWith( "```" ) ) {
            if ( inCode ) {
                html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\"><tr><td bgcolor=\"#f0f0e8\">"
                        "<pre><font color=\"#202020\">" + TQStyleSheet::escape( codeBlock ) + "</font></pre></td></tr></table>";
                codeBlock = TQString();
            }
            inCode = !inCode;
            continue;
        }
        if ( inCode ) {
            codeBlock += line + "\n";
            continue;
        }

        TQRegExp heading( "^(#{1,6})\\s+(.*)$" );
        TQRegExp bullet( "^(\\s*)[-*]\\s+(.*)$" );
        if ( heading.search( line ) >= 0 )
            html += "<b>" + inlineMarkup( heading.cap( 2 ) ) + "</b><br>";
        else if ( bullet.search( line ) >= 0 ) {
            for ( uint i = 0; i <= bullet.cap( 1 ).length() / 2; ++i )
                html += "&nbsp;&nbsp;";
            html += "&bull; " + inlineMarkup( bullet.cap( 2 ) ) + "<br>";
        }
        else
            html += inlineMarkup( line ) + "<br>";
    }
    if ( inCode )   // an answer that is still being written
        html += "<table width=\"100%\" cellpadding=\"4\" cellspacing=\"0\"><tr><td bgcolor=\"#f0f0e8\">"
                "<pre><font color=\"#202020\">" + TQStyleSheet::escape( codeBlock ) + "</font></pre></td></tr></table>";
    while ( html.endsWith( "<br>" ) )
        html.truncate( html.length() - 4 );
    return html;
}

/* ------------------------------------------------------------ attachments */

TQString ChatView::thumbnail( const TQString &path )
{
    TQFileInfo info( path );
    const TQString key = path + ":" + TQString::number( info.lastModified().toTime_t() );
    if ( m_thumbs.contains( key ) )
        return m_thumbs[key];

    TQString result;
    TQImage img( path );
    if ( !img.isNull() ) {
        const TQString ext = info.extension( false ).lower();
        const bool showable = ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif";
        if ( showable && img.width() <= thumbSize && img.height() <= thumbSize ) {
            result = path;
        } else {
            if ( img.width() > thumbSize || img.height() > thumbSize )
                img = img.smoothScale( thumbSize, thumbSize, TQImage::ScaleMin );
            KMD5 md5( TQCString( key.utf8() ) );
            const TQString file = locateLocal( "cache", "tde-ai-client/" ) + TQString::fromLatin1( md5.hexDigest() ) + ".png";
            if ( TQFile::exists( file ) || img.save( file, "PNG" ) )
                result = file;
        }
    }
    m_thumbs.insert( key, result );
    return result;
}

TQString ChatView::attachmentHtml( const Attachment &a, bool generated )
{
    const TQString href = KURL::encode_string( a.path );
    const TQString name = TQStyleSheet::escape( a.name() );
    TQString html;

    if ( !TQFile::exists( a.path ) )
        return "<p><i>" + i18n( "%1 (the file no longer exists)" ).arg( name ) + "</i></p>";

    if ( a.kind == Attachment::Image ) {
        const TQString thumb = thumbnail( a.path );
        if ( !thumb.isEmpty() )
            html += "<p><a href=\"open:" + href + "\"><img src=\"" + thumb + "\"></a></p>";
    }

    const TQString icon = TDEGlobal::iconLoader()->iconPath( KMimeType::mimeType( a.mime )->icon( TQString(), true ),
                                                            TDEIcon::Small, true );
    html += "<p>";
    if ( !icon.isEmpty() )
        html += "<img src=\"" + icon + "\"> ";
    html += "<a href=\"open:" + href + "\">" + name + "</a> <font size=\"-1\">("
            + Attachment::kindName( a.kind ) + ", " + TDEIO::convertSize( a.size() ) + ")";
    if ( generated )
        html += " &nbsp; <a href=\"save:" + href + "\">" + i18n( "Save As..." ) + "</a>";
    html += "</font></p>";
    return html;
}

/* -------------------------------------------------------------- rendering */

void ChatView::showMessages( const MessageList &messages, const TQString &pending, const TQString &welcome )
{
    const TQColorGroup cg = colorGroup();
    const TQString userBg = blend( cg.base(), cg.highlight(), 22 );
    const TQString aiBg = blend( cg.base(), cg.text(), 10 );
    const TQString dim = blend( cg.base(), cg.text(), 55 );

    TQString html;
    if ( messages.isEmpty() )
        html = "<br><center><font color=\"" + dim + "\">" + welcome + "</font></center>";

    MessageList::ConstIterator last = messages.end();
    if ( !messages.isEmpty() )
        --last;

    for ( MessageList::ConstIterator m = messages.begin(); m != messages.end(); ++m ) {
        const ChatMessage &msg = *m;

        if ( msg.role == ChatMessage::Info || msg.role == ChatMessage::Error ) {
            const TQString colour = msg.role == ChatMessage::Error ? "#b00020" : dim;
            const TQString label = msg.role == ChatMessage::Error ? i18n( "Error:" ) : i18n( "Note:" );
            html += "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\"><tr><td><font color=\"" + colour
                    + "\"><b>" + label + "</b> " + TQStyleSheet::escape( msg.text ).replace( "\n", "<br>" )
                    + "</font></td></tr></table>";
            continue;
        }

        const bool user = msg.role == ChatMessage::User;
        TQString header = user ? i18n( "You" ) : i18n( "AI" );
        header = "<b>" + header + "</b>";
        if ( !user && !msg.meta.isEmpty() )
            header += " <font size=\"-1\" color=\"" + dim + "\">" + TQStyleSheet::escape( msg.meta ) + "</font>";

        TQString body = formatText( msg.text );
        for ( AttachmentList::ConstIterator a = msg.attachments.begin(); a != msg.attachments.end(); ++a )
            body += attachmentHtml( *a, !user );
        if ( !user && m == last && !pending.isEmpty() ) {
            if ( !body.isEmpty() )
                body += "<br>";
            body += "<font color=\"" + dim + "\"><i>" + TQStyleSheet::escape( pending ) + "</i></font>";
        }

        html += "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\">"
                "<tr><td bgcolor=\"" + ( user ? userBg : aiBg ) + "\">" + header + "</td></tr>"
                "<tr><td>" + body + "</td></tr></table>";
    }

    TQScrollBar *bar = verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maxValue() - 30;
    const int x = contentsX(), y = contentsY();
    setText( html );
    if ( atEnd )
        scrollToBottom();
    else
        setContentsPos( x, y );
}

#include "chatview.moc"
