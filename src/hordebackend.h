/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_HORDEBACKEND_H
#define TDEAICLIENT_HORDEBACKEND_H

#include "backend.h"

class TQTimer;

/**
 * The AI Horde (https://aihorde.net): volunteers run text and image models,
 * requests wait in a queue and are polled until a worker has done them.
 */
class HordeBackend : public Backend
{
    TQ_OBJECT

public:
    HordeBackend( TQObject *parent );

    TQString id() const { return "horde"; }
    TQString name() const;
    TQString address() const;
    int capabilities() const;
    void refresh();

    /** The prompt sent for a conversation, in the configured prompt format. */
    static TQString buildPrompt( const MessageList &history, const TQMap<TQString, TQString> &captions,
                                 TQStringList &stopSequences );

protected:
    void run( const Request &request );
    void stop();

private slots:
    void slotTextModels( HttpRequest *r );
    void slotImageModels( HttpRequest *r );
    void slotUser( HttpRequest *r );
    void slotSubmitted( HttpRequest *r );
    void slotPoll();
    void slotStatus( HttpRequest *r );
    void slotImageResult( HttpRequest *r );
    void slotImageDownloaded( HttpRequest *r );

private:
    enum Job { NoJob, TextJob, ImageJob, CaptionJob };

    TQStringList headers() const;
    TQString url( const TQString &path ) const;
    void nextStep();
    void submitText();
    void submitImage();
    void submitCaption( const Attachment &image );
    void queueStatus( const TQString &what, const class JsonValue &state );

    Request        m_request;
    AttachmentList m_toCaption;
    Job            m_job;
    TQString       m_jobId;
    TQString       m_captionPath;
    TQTimer       *m_pollTimer;
};

#endif
