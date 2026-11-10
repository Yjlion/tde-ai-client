/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_KOBOLDBACKEND_H
#define TDEAICLIENT_KOBOLDBACKEND_H

#include "openaibackend.h"

/**
 * KoboldCpp running on this computer or in the local network.  Chat uses its
 * OpenAI-compatible endpoint (so the model's own chat template is applied);
 * capabilities, speech, transcription and image generation use the native
 * KoboldCpp and Stable Diffusion web APIs it provides.
 */
class KoboldBackend : public OpenAIBackend
{
    TQ_OBJECT

public:
    KoboldBackend( TQObject *parent );

    TQString id() const { return "koboldcpp"; }
    TQString name() const;
    TQString address() const;
    int capabilities() const { return m_caps; }
    bool ready() const { return m_probed; }
    void refresh();

protected:
    void stop();
    TQString apiKey() const;
    TQString chatUrl() const;
    bool transcribeAudio() const { return !m_nativeAudio; }
    void startTranscription( const Attachment &audio );
    void runImage( const Request &request );
    void runSpeech( const Request &request );
    TQString defaultChatModel() const { return "koboldcpp"; }

private slots:
    void slotVersion( HttpRequest *r );
    void slotModel( HttpRequest *r );
    void slotImageModels( HttpRequest *r );
    void slotKoboldTranscribed( HttpRequest *r );

private:
    int  m_caps;
    bool m_nativeAudio;
    bool m_probed;
};

#endif
