/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef TDEAICLIENT_SETTINGS_H
#define TDEAICLIENT_SETTINGS_H

#include <tqstring.h>
#include <tqstringlist.h>

/** Application settings, stored in tde-ai-clientrc (API keys optionally in TDEWallet). */
class Settings
{
public:
    enum PromptFormat { FormatAlpaca = 0, FormatChatML, FormatPlain };

    static Settings &self();

    void load();
    /** Returns false if the keys were to go to TDEWallet but it could not be used;
        they are then kept in the configuration file and useWallet is off. */
    bool save();
    void setDefaults();

    /** The model last chosen for @p backend ("horde", ...) in @p mode (Request::Mode). */
    TQString model( const TQString &backend, int mode ) const;
    void setModel( const TQString &backend, int mode, const TQString &model );

    static TQStringList promptFormatNames();
    static const char *anonymousHordeKey() { return "0000000000"; }

    /* general */
    TQString lastBackend;
    int      lastMode;
    bool     sendOnEnter;
    bool     useWallet;
    TQString systemPrompt;

    /* AI Horde */
    TQString hordeUrl;
    TQString hordeKey;
    int      hordePromptFormat;
    bool     hordeNsfw;
    bool     hordeTrustedOnly;
    bool     hordeSlowWorkers;

    /* KoboldCpp */
    TQString koboldUrl;
    TQString koboldKey;
    TQString koboldVoice;

    /* OpenAI-compatible */
    TQString openaiUrl;
    TQString openaiKey;
    bool     openaiTranscribeFirst;
    TQString openaiTranscribeModel;
    TQString openaiSpeechModel;
    TQString openaiVoice;

    /* generation */
    int      maxTokens;
    int      contextLength;
    double   temperature;
    double   topP;
    int      imageWidth;
    int      imageHeight;
    int      imageSteps;
    double   imageCfgScale;
    double   imageDenoise;
    TQString negativePrompt;

private:
    Settings();
    bool readWallet();
    bool writeWallet();
    static void protectFile();
};

#endif
