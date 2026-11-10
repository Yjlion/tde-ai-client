/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "httpclient.h"
#include "json.h"

#include <string.h>

#include <tqregexp.h>

#include <tdelocale.h>
#include <kurl.h>

HttpRequest::HttpRequest( const TQString &url, TQObject *parent )
    : TQObject( parent ), m_url( url ), m_status( 0 ), m_error( 0 ), m_errorPage( false )
{
}

HttpRequest *HttpRequest::get( const TQString &url, const TQStringList &headers, TQObject *parent )
{
    HttpRequest *r = new HttpRequest( url, parent );
    r->start( TDEIO::get( KURL( url ), true, false ), headers );
    return r;
}

HttpRequest *HttpRequest::post( const TQString &url, const TQByteArray &body, const TQString &contentType,
                                const TQStringList &headers, TQObject *parent )
{
    HttpRequest *r = new HttpRequest( url, parent );
    TDEIO::TransferJob *job = TDEIO::http_post( KURL( url ), body, false );
    job->addMetaData( "content-type", "Content-Type: " + contentType );
    r->start( job, headers );
    return r;
}

HttpRequest *HttpRequest::postJson( const TQString &url, const JsonValue &body,
                                    const TQStringList &headers, TQObject *parent )
{
    TQCString text = body.serialize();
    TQByteArray data;
    data.duplicate( text.data(), text.length() );
    return post( url, data, "application/json", headers, parent );
}

HttpRequest *HttpRequest::del( const TQString &url, const TQStringList &headers, TQObject *parent )
{
    HttpRequest *r = new HttpRequest( url, parent );
    r->start( TDEIO::file_delete( KURL( url ), false ), headers );
    return r;
}

void HttpRequest::start( TDEIO::Job *job, const TQStringList &headers )
{
    m_job = job;
    if ( !headers.isEmpty() )
        job->addMetaData( "customHTTPHeader", headers.join( "\r\n" ) );
    job->addMetaData( "UserAgent", "tde-ai-client/" VERSION );
    job->addMetaData( "accept", "*/*" );
    job->addMetaData( "cookies", "none" );
    job->addMetaData( "no-auth", "true" );
    job->addMetaData( "no-auth-prompt", "true" );
    job->addMetaData( "cache", "reload" );
    job->addMetaData( "UseCache", "false" );            // answers are not kept on disk
    job->addMetaData( "PropagateHttpHeader", "true" );  // for the status code

    if ( job->inherits( "TDEIO::TransferJob" ) ) {
        connect( job, TQ_SIGNAL( data( TDEIO::Job *, const TQByteArray & ) ),
                 this, TQ_SLOT( slotData( TDEIO::Job *, const TQByteArray & ) ) );
        connect( job, TQ_SIGNAL( mimetype( TDEIO::Job *, const TQString & ) ),
                 this, TQ_SLOT( slotMimetype( TDEIO::Job *, const TQString & ) ) );
    }
    connect( job, TQ_SIGNAL( result( TDEIO::Job * ) ), this, TQ_SLOT( slotResult( TDEIO::Job * ) ) );
}

void HttpRequest::slotData( TDEIO::Job *, const TQByteArray &data )
{
    if ( data.isEmpty() )
        return;
    const uint old = m_body.size();
    m_body.resize( old + data.size() );
    memcpy( m_body.data() + old, data.data(), data.size() );
    emit chunk( data );
}

void HttpRequest::slotMimetype( TDEIO::Job *, const TQString &type )
{
    m_contentType = type;
}

void HttpRequest::slotResult( TDEIO::Job *job )
{
    m_error = job->error();
    if ( m_error )
        m_errorText = job->errorString();

    if ( job->inherits( "TDEIO::TransferJob" ) )
        m_errorPage = static_cast<TDEIO::TransferJob *>( job )->isErrorPage();

    const TQString headers = job->queryMetaData( "HTTP-Headers" );
    TQRegExp statusLine( "HTTP/[0-9.]+\\s+(\\d+)" );
    int pos = 0, last = -1;
    while ( ( pos = statusLine.search( headers, pos ) ) >= 0 ) {    // the last one: after redirects
        last = statusLine.cap( 1 ).toInt();
        pos += statusLine.matchedLength();
    }
    if ( last >= 0 )
        m_status = last;
    TQRegExp ctype( "content-type:\\s*([^;\\r\\n]+)", false );
    if ( ctype.search( headers ) >= 0 )
        m_contentType = ctype.cap( 1 ).stripWhiteSpace().lower();

    m_job = 0;
    emit finished( this );
    deleteLater();
}

void HttpRequest::abort()
{
    if ( m_job ) {
        m_job->disconnect( this );
        m_job->kill( true );
        m_job = 0;
    }
    deleteLater();
}

bool HttpRequest::ok() const
{
    if ( m_error || m_errorPage )
        return false;
    return m_status == 0 || ( m_status >= 200 && m_status < 300 );
}

JsonValue HttpRequest::json() const
{
    return JsonValue::parse( m_body );
}

TQString HttpRequest::errorString() const
{
    if ( m_error )
        return m_errorText;

    /* The services explain errors in JSON, each in its own way. */
    TQString detail;
    bool isJson = false;
    JsonValue v = JsonValue::parse( m_body, &isJson );
    if ( isJson && v.isObject() ) {
        const JsonValue &err = v.get( "error" );
        if ( err.isObject() )
            detail = err.get( "message" ).toString();
        else if ( err.isString() )
            detail = err.toString();
        if ( detail.isEmpty() )
            detail = v.get( "message" ).toString();
        if ( detail.isEmpty() ) {
            const JsonValue &d = v.get( "detail" );
            detail = d.isObject() ? d.get( "msg" ).toString() : d.toString();
        }
    }
    if ( detail.isEmpty() && m_body.size() > 0 && m_body.size() < 300 && !isJson )
        detail = TQString::fromUtf8( m_body.data(), m_body.size() ).stripWhiteSpace();

    if ( m_status == 0 )
        return detail.isEmpty() ? i18n( "The server refused the request." ) : detail;
    if ( m_status >= 400 ) {
        if ( detail.isEmpty() )
            return i18n( "The server answered with HTTP error %1." ).arg( m_status );
        return i18n( "%1 (HTTP error %2)" ).arg( detail ).arg( m_status );
    }
    return detail.isEmpty() ? i18n( "The server sent an answer that could not be understood." ) : detail;
}

#include "httpclient.moc"
