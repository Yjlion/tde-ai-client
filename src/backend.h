/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_BACKEND_H
#define TDEAICLIENT_BACKEND_H

#include "types.h"

#include <tqmap.h>
#include <tqobject.h>
#include <tqptrlist.h>
#include <tqstringlist.h>

class HttpRequest;

/**
 * An AI service.  A backend does one Request at a time: start() it, then
 * wait for finished() or failed(), or cancel() it.
 */
class Backend : public TQObject
{
    TQ_OBJECT

public:
    enum Capability {
        CapChat         = 0x001,
        CapImageInput   = 0x002,    ///< images can be attached to chat messages
        CapAudioInput   = 0x004,    ///< audio files can be attached to chat messages
        CapVideoInput   = 0x008,
        CapFileInput    = 0x010,    ///< any other file (PDF, ...)
        CapImageGen     = 0x020,
        CapImageToImage = 0x040,    ///< image generation can start from an attached image
        CapSpeech       = 0x080     ///< text to speech
    };

    Backend( TQObject *parent, const char *name );
    virtual ~Backend();

    virtual TQString id() const = 0;            ///< "horde", "koboldcpp", "openai"
    virtual TQString name() const = 0;          ///< translated, for the user
    virtual TQString address() const = 0;       ///< where the service is reached
    virtual int capabilities() const = 0;
    /** False while it is not yet known what the service can do. */
    virtual bool ready() const { return true; }

    bool supports( Request::Mode mode ) const;
    /** Whether a file of @p kind can be sent along in @p mode. */
    bool accepts( Request::Mode mode, Attachment::Kind kind ) const;

    TQStringList models( Request::Mode mode ) const { return m_models[mode]; }
    bool busy() const { return m_busy; }

    /** Asks the service what it offers: models, capabilities, account. */
    virtual void refresh() = 0;

    void start( const Request &request );
    void cancel();

signals:
    void textDelta( const TQString &text );         ///< more answer text
    void media( const Attachment &file );           ///< a generated image, sound, ...
    void meta( const TQString &text );              ///< the model that answers
    void status( const TQString &text );            ///< progress, for the status bar
    void note( const TQString &text );              ///< about the request, shown in the chat
    void serviceMessage( const TQString &text );    ///< about the service, for the status bar
    void finished();
    void failed( const TQString &message );
    void modelsChanged();
    void capabilitiesChanged();
    void accountInfo( const TQString &text );

protected:
    virtual void run( const Request &request ) = 0;
    virtual void stop() {}

    void done();
    void fail( const TQString &message );
    void setModels( Request::Mode mode, const TQStringList &models );

    /** Keeps track of @p request so that cancel() aborts it. */
    HttpRequest *track( HttpRequest *request );
    void abortRequests();

    /** The text of @p message followed by the contents of its text attachments. */
    static TQString textWithFiles( const ChatMessage &message );
    /** Stores received data below the application's data directory.
        Images in formats TQt cannot show inline (WebP) are converted to PNG. */
    static Attachment saveMedia( const TQByteArray &data, const TQString &mime );
    /** Rounds to what image generators expect: a multiple of 64, at least 64. */
    static int imageDimension( int value );

    /** Text derived from attachments (transcripts, captions), by file path. */
    TQMap<TQString, TQString> m_derived;

private slots:
    void slotRequestGone( HttpRequest *request );

private:
    TQStringList           m_models[Request::ModeCount];
    TQPtrList<HttpRequest> m_requests;
    bool                   m_busy;
};

#endif
