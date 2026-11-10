/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TDEAICLIENT_HTTPCLIENT_H
#define TDEAICLIENT_HTTPCLIENT_H

#include <tqcstring.h>
#include <tqguardedptr.h>
#include <tqobject.h>
#include <tqstringlist.h>

#include <tdeio/job.h>

class JsonValue;

/**
 * One HTTP request made through the TDE I/O framework (so proxy settings
 * and TLS certificates are those of the desktop).  The object deletes
 * itself after finished() has been emitted or abort() was called.
 */
class HttpRequest : public TQObject
{
    TQ_OBJECT

public:
    /** @p headers are complete lines such as "apikey: 0000000000". */
    static HttpRequest *get( const TQString &url, const TQStringList &headers, TQObject *parent );
    static HttpRequest *post( const TQString &url, const TQByteArray &body, const TQString &contentType,
                              const TQStringList &headers, TQObject *parent );
    static HttpRequest *postJson( const TQString &url, const JsonValue &body,
                                  const TQStringList &headers, TQObject *parent );
    static HttpRequest *del( const TQString &url, const TQStringList &headers, TQObject *parent );

    /** True if the transfer worked and the server answered with a 2xx status. */
    bool ok() const;
    int status() const { return m_status; }
    const TQByteArray &body() const { return m_body; }
    JsonValue json() const;
    TQString contentType() const { return m_contentType; }
    /** A message for the user; includes the server's explanation if it sent one. */
    TQString errorString() const;
    TQString url() const { return m_url; }

    /** Stops the transfer; finished() is not emitted. */
    void abort();

signals:
    /** Newly arrived body data (the data is also collected in body()). */
    void chunk( const TQByteArray &data );
    void finished( HttpRequest *request );

private slots:
    void slotData( TDEIO::Job *job, const TQByteArray &data );
    void slotResult( TDEIO::Job *job );
    void slotMimetype( TDEIO::Job *job, const TQString &type );

private:
    HttpRequest( const TQString &url, TQObject *parent );
    void start( TDEIO::Job *job, const TQStringList &headers );

    TQString                 m_url;
    TQGuardedPtr<TDEIO::Job> m_job;
    TQByteArray              m_body;
    TQString                 m_contentType;
    TQString                 m_errorText;
    int                      m_status;
    int                      m_error;
    bool                     m_errorPage;
};

#endif
