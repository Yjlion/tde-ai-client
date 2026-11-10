/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_OPENAIBACKEND_H
#define TDEAICLIENT_OPENAIBACKEND_H

#include "backend.h"

class JsonValue;

/**
 * Any service that speaks the OpenAI REST API: OpenAI itself, OpenRouter,
 * Ollama, llama.cpp, LocalAI, vLLM, ...  Also the base of the KoboldCpp
 * backend, which offers the same chat endpoint.
 */
class OpenAIBackend : public Backend
{
    TQ_OBJECT

public:
    OpenAIBackend( TQObject *parent, const char *name = "openai backend" );

    TQString id() const { return "openai"; }
    TQString name() const;
    TQString address() const;
    int capabilities() const;
    void refresh();

protected:
    void run( const Request &request );
    void stop();

    virtual TQString apiKey() const;
    virtual TQString chatUrl() const;
    /** Whether audio attachments are turned into text before the chat request. */
    virtual bool transcribeAudio() const;
    virtual void startTranscription( const Attachment &audio );
    virtual void runImage( const Request &request );
    virtual void runSpeech( const Request &request );
    /** The "model" value for chat requests when the user chose none. */
    virtual TQString defaultChatModel() const;

    TQStringList headers() const;
    void transcriptReady( const TQString &text );
    static TQString trimmedUrl( const TQString &url );

protected slots:
    void slotModels( HttpRequest *r );
    void slotChatData( const TQByteArray &data );
    void slotChatFinished( HttpRequest *r );
    void slotTranscribed( HttpRequest *r );
    void slotImageResult( HttpRequest *r );
    void slotImageDownloaded( HttpRequest *r );
    void slotSpeechResult( HttpRequest *r );

private:
    void nextStep();
    void sendChat();
    JsonValue messageContent( const ChatMessage &message ) const;
    void handleEvent( const TQCString &payload );

    Request        m_request;
    AttachmentList m_toTranscribe;
    TQString       m_transcribing;
    TQCString      m_sse;
    bool           m_gotText;
    bool           m_gotMeta;
    TQString       m_streamError;
};

#endif
