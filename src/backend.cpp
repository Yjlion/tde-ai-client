/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "backend.h"
#include "conversationstore.h"
#include "httpclient.h"

#include <string.h>

#include <tqfile.h>
#include <tqimage.h>

#include <tdelocale.h>

Backend::Backend( TQObject *parent, const char *name )
    : TQObject( parent, name ), m_busy( false )
{
}

Backend::~Backend()
{
}

bool Backend::supports( Request::Mode mode ) const
{
    switch ( mode ) {
    case Request::Chat:   return capabilities() & CapChat;
    case Request::Image:  return capabilities() & CapImageGen;
    case Request::Speech: return capabilities() & CapSpeech;
    default:              return false;
    }
}

bool Backend::accepts( Request::Mode mode, Attachment::Kind kind ) const
{
    const int caps = capabilities();
    if ( mode == Request::Image )
        return kind == Attachment::Image && ( caps & CapImageToImage );
    if ( mode != Request::Chat )
        return false;
    switch ( kind ) {
    case Attachment::Text:  return true;
    case Attachment::Image: return caps & CapImageInput;
    case Attachment::Audio: return caps & CapAudioInput;
    case Attachment::Video: return caps & CapVideoInput;
    default:                return caps & CapFileInput;
    }
}

void Backend::start( const Request &request )
{
    if ( m_busy )
        return;
    m_busy = true;
    run( request );
}

void Backend::cancel()
{
    if ( !m_busy )
        return;
    stop();
    abortRequests();
    m_busy = false;
}

void Backend::done()
{
    if ( !m_busy )
        return;
    m_busy = false;
    emit finished();
}

void Backend::fail( const TQString &message )
{
    if ( !m_busy )
        return;
    stop();
    abortRequests();
    m_busy = false;
    emit failed( message );
}

void Backend::setModels( Request::Mode mode, const TQStringList &models )
{
    m_models[mode] = models;
    emit modelsChanged();
}

HttpRequest *Backend::track( HttpRequest *request )
{
    m_requests.append( request );
    connect( request, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotRequestGone( HttpRequest * ) ) );
    return request;
}

void Backend::slotRequestGone( HttpRequest *request )
{
    m_requests.removeRef( request );
}

void Backend::abortRequests()
{
    TQPtrList<HttpRequest> pending = m_requests;
    m_requests.clear();
    for ( HttpRequest *r = pending.first(); r; r = pending.next() )
        r->abort();
}

TQString Backend::textWithFiles( const ChatMessage &message )
{
    static const uint maxBytes = 256 * 1024;

    TQString text = message.text;
    for ( AttachmentList::ConstIterator a = message.attachments.begin(); a != message.attachments.end(); ++a ) {
        if ( ( *a ).kind != Attachment::Text || !TQFile::exists( ( *a ).path ) )
            continue;
        TQString content = ( *a ).text();
        if ( content.length() > maxBytes )
            content = content.left( maxBytes ) + "\n" + i18n( "[the rest of the file was left out]" );
        text += "\n\n" + i18n( "File \"%1\":" ).arg( ( *a ).name() ) + "\n```\n" + content;
        if ( !content.endsWith( "\n" ) )
            text += "\n";
        text += "```";
    }
    return text.stripWhiteSpace();
}

Attachment Backend::saveMedia( const TQByteArray &data, const TQString &mimeType )
{
    TQString mime = mimeType;
    if ( mime.isEmpty() || mime == "application/octet-stream" ) {
        /* guess from the first bytes */
        const char *d = data.data();
        const uint n = data.size();
        if ( n > 8 && memcmp( d, "\x89PNG", 4 ) == 0 )
            mime = "image/png";
        else if ( n > 3 && memcmp( d, "\xff\xd8\xff", 3 ) == 0 )
            mime = "image/jpeg";
        else if ( n > 12 && memcmp( d, "RIFF", 4 ) == 0 && memcmp( d + 8, "WEBP", 4 ) == 0 )
            mime = "image/webp";
        else if ( n > 12 && memcmp( d, "RIFF", 4 ) == 0 && memcmp( d + 8, "WAVE", 4 ) == 0 )
            mime = "audio/x-wav";
        else if ( n > 3 && ( memcmp( d, "ID3", 3 ) == 0 || ( (uchar) d[0] == 0xff && ( (uchar) d[1] & 0xe0 ) == 0xe0 ) ) )
            mime = "audio/mpeg";
        else if ( n > 4 && memcmp( d, "OggS", 4 ) == 0 )
            mime = "audio/ogg";
        else if ( n > 4 && memcmp( d, "fLaC", 4 ) == 0 )
            mime = "audio/flac";
        else if ( n > 12 && memcmp( d + 4, "ftyp", 4 ) == 0 )
            mime = "video/mp4";
        else
            mime = "application/octet-stream";
    }

    Attachment a;
    a.mime = mime;
    a.kind = Attachment::kindForMime( mime );
    a.path = ConversationStore::newMediaPath( ConversationStore::extensionForMime( mime ) );

    if ( a.kind == Attachment::Image && mime != "image/png" && mime != "image/jpeg" && mime != "image/gif" ) {
        TQImage img;
        if ( img.loadFromData( data ) ) {
            a.mime = "image/png";
            a.path = ConversationStore::newMediaPath( "png" );
            if ( img.save( a.path, "PNG" ) )
                return a;
            a.mime = mime;
            a.path = ConversationStore::newMediaPath( ConversationStore::extensionForMime( mime ) );
        }
    }

    TQFile f( a.path );
    if ( f.open( IO_WriteOnly ) ) {
        f.writeBlock( data.data(), data.size() );
        f.close();
    }
    return a;
}

int Backend::imageDimension( int value )
{
    int v = ( ( value + 32 ) / 64 ) * 64;
    return v < 64 ? 64 : v;
}

#include "backend.moc"
