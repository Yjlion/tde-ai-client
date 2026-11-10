/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TDEAICLIENT_CONVERSATIONSTORE_H
#define TDEAICLIENT_CONVERSATIONSTORE_H

#include "types.h"

/** Keeps conversations as JSON files below $TDEHOME/share/apps/tde-ai-client. */
namespace ConversationStore
{
    /** All stored conversations, most recently changed first. */
    TQValueList<Conversation> loadAll();
    bool save( Conversation &conversation );
    void remove( const TQString &id );
    TQString newId();

    /** A new file name for data received from a service; @p extension without dot. */
    TQString newMediaPath( const TQString &extension );
    /** File name extension for a MIME type ("png", "wav", ...). */
    TQString extensionForMime( const TQString &mime );

    /** Writes the conversation as plain text or, if @p html, as a web page. */
    bool exportTo( const Conversation &conversation, const TQString &path, bool html );
}

#endif
