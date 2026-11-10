/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "conversationstore.h"
#include "json.h"

#include <stdlib.h>

#include <tqdatetime.h>
#include <tqdir.h>
#include <tqfile.h>
#include <tqstylesheet.h>
#include <tqtextstream.h>

#include <tdeglobal.h>
#include <tdelocale.h>
#include <kmimetype.h>
#include <kstandarddirs.h>

static TQString conversationDir()
{
    return locateLocal( "appdata", "conversations/" );
}

TQString ConversationStore::newId()
{
    static int counter = 0;
    return TQDateTime::currentDateTime().toString( "yyyyMMdd-hhmmss" )
           + TQString( "-%1" ).arg( ++counter );
}

TQString ConversationStore::newMediaPath( const TQString &extension )
{
    static int counter = 0;
    const TQString dir = locateLocal( "appdata", "media/" );
    TQString path;
    do {
        path = dir + TQDateTime::currentDateTime().toString( "yyyyMMdd-hhmmss" )
               + TQString( "-%1.%2" ).arg( ++counter ).arg( extension );
    } while ( TQFile::exists( path ) );
    return path;
}

TQString ConversationStore::extensionForMime( const TQString &mime )
{
    static const struct { const char *mime; const char *ext; } known[] = {
        { "image/png", "png" }, { "image/jpeg", "jpg" }, { "image/webp", "webp" }, { "image/gif", "gif" },
        { "audio/mpeg", "mp3" }, { "audio/mp3", "mp3" }, { "audio/wav", "wav" }, { "audio/x-wav", "wav" },
        { "audio/wave", "wav" }, { "audio/ogg", "ogg" }, { "audio/opus", "opus" }, { "audio/flac", "flac" },
        { "audio/aac", "aac" }, { "video/mp4", "mp4" }, { "video/webm", "webm" }, { 0, 0 }
    };
    for ( int i = 0; known[i].mime; ++i )
        if ( mime == known[i].mime )
            return known[i].ext;
    KMimeType::Ptr type = KMimeType::mimeType( mime );
    if ( type && !type->patterns().isEmpty() ) {
        TQString p = type->patterns().first();
        if ( p.startsWith( "*." ) )
            return p.mid( 2 );
    }
    return "bin";
}

static bool newerFirst( const Conversation &a, const Conversation &b )
{
    return a.modified > b.modified;
}

TQValueList<Conversation> ConversationStore::loadAll()
{
    TQValueList<Conversation> result;
    TQDir dir( conversationDir() );
    const TQStringList files = dir.entryList( "*.json", TQDir::Files );
    for ( TQStringList::ConstIterator f = files.begin(); f != files.end(); ++f ) {
        TQFile file( dir.filePath( *f ) );
        if ( !file.open( IO_ReadOnly ) )
            continue;
        bool ok = false;
        JsonValue doc = JsonValue::parse( file.readAll(), &ok );
        if ( !ok || !doc.isObject() )
            continue;

        Conversation c;
        c.id = ( *f ).left( ( *f ).length() - 5 );
        c.title = doc.get( "title" ).toString();
        c.backend = doc.get( "backend" ).toString();
        c.modified = (uint) doc.get( "modified" ).toDouble();
        const JsonValue &msgs = doc.get( "messages" );
        for ( uint i = 0; i < msgs.count(); ++i ) {
            const JsonValue &m = msgs.at( i );
            ChatMessage msg;
            const TQString role = m.get( "role" ).toString();
            msg.role = role == "assistant" ? ChatMessage::Assistant
                     : role == "info" ? ChatMessage::Info
                     : role == "error" ? ChatMessage::Error : ChatMessage::User;
            msg.text = m.get( "text" ).toString();
            msg.meta = m.get( "meta" ).toString();
            const JsonValue &atts = m.get( "attachments" );
            for ( uint j = 0; j < atts.count(); ++j ) {
                Attachment a;
                a.path = atts.at( j ).get( "path" ).toString();
                a.mime = atts.at( j ).get( "mime" ).toString();
                a.kind = Attachment::kindForMime( a.mime );
                msg.attachments.append( a );
            }
            c.messages.append( msg );
        }
        result.append( c );
    }

    /* small lists: a simple insertion sort keeps this free of template helpers */
    TQValueList<Conversation> sorted;
    for ( TQValueList<Conversation>::ConstIterator it = result.begin(); it != result.end(); ++it ) {
        TQValueList<Conversation>::Iterator pos = sorted.begin();
        while ( pos != sorted.end() && !newerFirst( *it, *pos ) )
            ++pos;
        sorted.insert( pos, *it );
    }
    return sorted;
}

bool ConversationStore::save( Conversation &c )
{
    if ( c.id.isEmpty() )
        c.id = newId();
    c.modified = TQDateTime::currentDateTime().toTime_t();

    JsonValue doc = JsonValue::object();
    doc.set( "title", c.title );
    doc.set( "backend", c.backend );
    doc.set( "modified", (double) c.modified );
    JsonValue msgs = JsonValue::array();
    for ( MessageList::ConstIterator m = c.messages.begin(); m != c.messages.end(); ++m ) {
        JsonValue msg = JsonValue::object();
        static const char *const roles[] = { "user", "assistant", "info", "error" };
        msg.set( "role", roles[( *m ).role] );
        msg.set( "text", ( *m ).text );
        if ( !( *m ).meta.isEmpty() )
            msg.set( "meta", ( *m ).meta );
        if ( !( *m ).attachments.isEmpty() ) {
            JsonValue atts = JsonValue::array();
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a ) {
                JsonValue att = JsonValue::object();
                att.set( "path", ( *a ).path );
                att.set( "mime", ( *a ).mime );
                atts.append( att );
            }
            msg.set( "attachments", atts );
        }
        msgs.append( msg );
    }
    doc.set( "messages", msgs );

    TQFile file( conversationDir() + c.id + ".json" );
    if ( !file.open( IO_WriteOnly ) )
        return false;
    const TQCString text = doc.serialize();
    return file.writeBlock( text.data(), text.length() ) == (TQ_LONG) text.length();
}

void ConversationStore::remove( const TQString &id )
{
    if ( !id.isEmpty() )
        TQFile::remove( conversationDir() + id + ".json" );
}

bool ConversationStore::exportTo( const Conversation &c, const TQString &path, bool html )
{
    TQFile file( path );
    if ( !file.open( IO_WriteOnly ) )
        return false;
    TQTextStream out( &file );
    out.setEncoding( TQTextStream::UnicodeUTF8 );

    if ( html )
        out << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>"
            << TQStyleSheet::escape( c.title ) << "</title></head><body>\n<h1>"
            << TQStyleSheet::escape( c.title ) << "</h1>\n";
    else
        out << c.title << "\n\n";

    for ( MessageList::ConstIterator m = c.messages.begin(); m != c.messages.end(); ++m ) {
        TQString who;
        switch ( ( *m ).role ) {
        case ChatMessage::User:      who = i18n( "You" ); break;
        case ChatMessage::Assistant: who = ( *m ).meta.isEmpty() ? i18n( "AI" ) : i18n( "AI (%1)" ).arg( ( *m ).meta ); break;
        case ChatMessage::Info:      who = i18n( "Note" ); break;
        default:                     who = i18n( "Error" );
        }
        if ( html ) {
            out << "<h3>" << TQStyleSheet::escape( who ) << "</h3>\n<p style=\"white-space: pre-wrap\">"
                << TQStyleSheet::escape( ( *m ).text ) << "</p>\n";
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a ) {
                const TQString p = TQStyleSheet::escape( ( *a ).path );
                if ( ( *a ).kind == Attachment::Image )
                    out << "<p><img src=\"file://" << p << "\" style=\"max-width: 512px\"></p>\n";
                else
                    out << "<p><a href=\"file://" << p << "\">" << TQStyleSheet::escape( ( *a ).name() ) << "</a></p>\n";
            }
        } else {
            out << "--- " << who << " ---\n" << ( *m ).text << "\n";
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a )
                out << "[" << ( *a ).path << "]\n";
            out << "\n";
        }
    }
    if ( html )
        out << "</body></html>\n";
    return file.status() == IO_Ok;
}
