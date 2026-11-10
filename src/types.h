/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TDEAICLIENT_TYPES_H
#define TDEAICLIENT_TYPES_H

#include <tqcstring.h>
#include <tqstring.h>
#include <tqvaluelist.h>

/** A file that is sent to, or was received from, an AI service. */
struct Attachment
{
    enum Kind { Text = 0, Image, Audio, Video, Other };

    Attachment() : kind( Other ) {}

    /** Describes @p path; the kind is derived from its MIME type. */
    static Attachment fromFile( const TQString &path );
    static Kind kindForMime( const TQString &mime );
    /** Translated name of a kind, e.g. "image". */
    static TQString kindName( Kind kind );

    TQString name() const;              ///< file name without directory
    TQByteArray data() const;           ///< file contents (empty if unreadable)
    TQString base64() const;            ///< file contents, base64 encoded
    TQString dataUri() const;           ///< data:<mime>;base64,<...>
    TQString text() const;              ///< file contents decoded as UTF-8 text
    uint size() const;

    TQString path;
    TQString mime;
    Kind     kind;
};

typedef TQValueList<Attachment> AttachmentList;

struct ChatMessage
{
    enum Role { User = 0, Assistant, Info, Error };

    ChatMessage() : role( User ) {}
    ChatMessage( Role r, const TQString &t ) : role( r ), text( t ) {}

    Role           role;
    TQString       text;
    TQString       meta;        ///< model / worker that produced the message
    AttachmentList attachments;
};

typedef TQValueList<ChatMessage> MessageList;

struct Conversation
{
    TQString    id;
    TQString    title;
    TQString    backend;
    uint        modified;       ///< seconds since the epoch
    MessageList messages;

    Conversation() : modified( 0 ) {}
};

/** What the user asks a backend to do. */
struct Request
{
    enum Mode { Chat = 0, Image, Speech, ModeCount };

    Request() : mode( Chat ) {}

    Mode           mode;
    MessageList    history;     ///< Chat: the conversation, ending with the new user message
    TQString       prompt;      ///< Image / Speech: the text to work on
    AttachmentList attachments; ///< Image: optional source image
    TQString       model;       ///< empty = let the service decide
};

#endif
