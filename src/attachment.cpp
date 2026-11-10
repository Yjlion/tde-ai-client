/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "types.h"

#include <tqfile.h>
#include <tqfileinfo.h>

#include <tdelocale.h>
#include <kmdcodec.h>
#include <kmimetype.h>

Attachment::Kind Attachment::kindForMime( const TQString &mime )
{
    if ( mime.startsWith( "image/" ) )
        return Image;
    if ( mime.startsWith( "audio/" ) || mime == "application/ogg" )
        return Audio;
    if ( mime.startsWith( "video/" ) )
        return Video;
    if ( mime.startsWith( "text/" ) )
        return Text;

    static const char *const textual[] = {
        "application/json", "application/xml", "application/x-shellscript", "application/x-perl",
        "application/x-python", "application/x-php", "application/x-ruby", "application/javascript",
        "application/x-javascript", "application/x-desktop", "application/x-yaml", "application/x-tex",
        "application/x-troff", "application/x-troff-man", "application/sql", "application/x-sql",
        "application/x-java", "application/x-csh", "application/x-awk", "application/x-subrip",
        "application/toml", "application/xhtml+xml", "application/x-zerosize", 0
    };
    for ( int i = 0; textual[i]; ++i )
        if ( mime == textual[i] )
            return Text;

    KMimeType::Ptr type = KMimeType::mimeType( mime );
    if ( type && type->is( "text/plain" ) )
        return Text;
    return Other;
}

/* Files whose type TDE does not know, but which contain no NUL bytes in
   the first few kilobytes, are most likely source code or notes. */
static bool looksLikeText( const TQString &path )
{
    TQFile f( path );
    if ( !f.open( IO_ReadOnly ) )
        return false;
    char buf[4096];
    TQ_LONG n = f.readBlock( buf, sizeof( buf ) );
    for ( TQ_LONG i = 0; i < n; ++i )
        if ( buf[i] == 0 )
            return false;
    return true;
}

Attachment Attachment::fromFile( const TQString &path )
{
    Attachment a;
    a.path = path;
    a.mime = KMimeType::findByPath( path )->name();
    a.kind = kindForMime( a.mime );
    if ( a.kind == Other && a.mime == "application/octet-stream" && looksLikeText( path ) ) {
        a.kind = Text;
        a.mime = "text/plain";
    }
    return a;
}

TQString Attachment::kindName( Kind kind )
{
    switch ( kind ) {
    case Text:  return i18n( "text" );
    case Image: return i18n( "image" );
    case Audio: return i18n( "audio" );
    case Video: return i18n( "video" );
    default:    return i18n( "file" );
    }
}

TQString Attachment::name() const
{
    return TQFileInfo( path ).fileName();
}

uint Attachment::size() const
{
    return TQFileInfo( path ).size();
}

TQByteArray Attachment::data() const
{
    TQFile f( path );
    if ( !f.open( IO_ReadOnly ) )
        return TQByteArray();
    return f.readAll();
}

TQString Attachment::base64() const
{
    return TQString::fromLatin1( KCodecs::base64Encode( data(), false ) );
}

TQString Attachment::dataUri() const
{
    return "data:" + mime + ";base64," + base64();
}

TQString Attachment::text() const
{
    TQByteArray d = data();
    return TQString::fromUtf8( d.data(), d.size() );
}
