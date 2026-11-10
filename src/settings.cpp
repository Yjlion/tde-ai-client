/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "settings.h"

#include <sys/stat.h>

#include <tqfile.h>

#include <tdeapplication.h>
#include <tdeconfig.h>
#include <tdeglobal.h>
#include <tdelocale.h>
#include <kstandarddirs.h>
#include <tdewallet.h>

static const char walletFolder[] = "tde-ai-client";

Settings &Settings::self()
{
    static Settings *s = 0;
    if ( !s ) {
        s = new Settings;
        s->load();
    }
    return *s;
}

Settings::Settings()
{
    setDefaults();
}

void Settings::setDefaults()
{
    lastBackend = "horde";
    lastMode = 0;
    sendOnEnter = true;
    useWallet = false;
    systemPrompt = TQString();

    hordeUrl = "https://aihorde.net/api/v2";
    hordeKey = anonymousHordeKey();
    hordePromptFormat = FormatAlpaca;
    hordeNsfw = false;
    hordeTrustedOnly = false;
    hordeSlowWorkers = true;

    koboldUrl = "http://localhost:5001";
    koboldKey = TQString();
    koboldVoice = "kobo";

    openaiUrl = "https://api.openai.com/v1";
    openaiKey = TQString();
    openaiTranscribeFirst = false;
    openaiTranscribeModel = "whisper-1";
    openaiSpeechModel = "tts-1";
    openaiVoice = "alloy";

    maxTokens = 512;
    contextLength = 4096;
    temperature = 0.7;
    topP = 0.9;
    imageWidth = 512;
    imageHeight = 512;
    imageSteps = 25;
    imageCfgScale = 7.0;
    imageDenoise = 0.6;
    negativePrompt = TQString();
}

TQStringList Settings::promptFormatNames()
{
    TQStringList l;
    l << i18n( "Alpaca (### Instruction / ### Response)" )
      << i18n( "ChatML (<|im_start|> ... <|im_end|>)" )
      << i18n( "Plain (User: / Assistant:)" );
    return l;
}

void Settings::load()
{
    setDefaults();
    TDEConfig *c = TDEGlobal::config();

    c->setGroup( "General" );
    lastBackend = c->readEntry( "LastBackend", lastBackend );
    lastMode = c->readNumEntry( "LastMode", lastMode );
    sendOnEnter = c->readBoolEntry( "SendOnEnter", sendOnEnter );
    useWallet = c->readBoolEntry( "UseWallet", useWallet );
    systemPrompt = c->readEntry( "SystemPrompt", systemPrompt );

    c->setGroup( "AI Horde" );
    hordeUrl = c->readEntry( "Url", hordeUrl );
    hordeKey = c->readEntry( "ApiKey", hordeKey );
    hordePromptFormat = c->readNumEntry( "PromptFormat", hordePromptFormat );
    hordeNsfw = c->readBoolEntry( "AllowNsfw", hordeNsfw );
    hordeTrustedOnly = c->readBoolEntry( "TrustedWorkersOnly", hordeTrustedOnly );
    hordeSlowWorkers = c->readBoolEntry( "SlowWorkers", hordeSlowWorkers );

    c->setGroup( "KoboldCpp" );
    koboldUrl = c->readEntry( "Url", koboldUrl );
    koboldKey = c->readEntry( "ApiKey", koboldKey );
    koboldVoice = c->readEntry( "Voice", koboldVoice );

    c->setGroup( "OpenAI" );
    openaiUrl = c->readEntry( "Url", openaiUrl );
    openaiKey = c->readEntry( "ApiKey", openaiKey );
    openaiTranscribeFirst = c->readBoolEntry( "TranscribeAudioFirst", openaiTranscribeFirst );
    openaiTranscribeModel = c->readEntry( "TranscribeModel", openaiTranscribeModel );
    openaiSpeechModel = c->readEntry( "SpeechModel", openaiSpeechModel );
    openaiVoice = c->readEntry( "Voice", openaiVoice );

    c->setGroup( "Generation" );
    maxTokens = c->readNumEntry( "MaxTokens", maxTokens );
    contextLength = c->readNumEntry( "ContextLength", contextLength );
    temperature = c->readDoubleNumEntry( "Temperature", temperature );
    topP = c->readDoubleNumEntry( "TopP", topP );
    imageWidth = c->readNumEntry( "ImageWidth", imageWidth );
    imageHeight = c->readNumEntry( "ImageHeight", imageHeight );
    imageSteps = c->readNumEntry( "ImageSteps", imageSteps );
    imageCfgScale = c->readDoubleNumEntry( "ImageCfgScale", imageCfgScale );
    imageDenoise = c->readDoubleNumEntry( "ImageDenoise", imageDenoise );
    negativePrompt = c->readEntry( "NegativePrompt", negativePrompt );

    if ( useWallet )
        readWallet();
    protectFile();
}

bool Settings::save()
{
    TDEConfig *c = TDEGlobal::config();

    /* Keys go either to the wallet or to the configuration file, never both.
       Without a usable wallet they stay in the file, and the option is off
       again so that they are found there at the next start. */
    const bool wanted = useWallet;
    const bool inWallet = useWallet && writeWallet();
    useWallet = inWallet;

    c->setGroup( "General" );
    c->writeEntry( "LastBackend", lastBackend );
    c->writeEntry( "LastMode", lastMode );
    c->writeEntry( "SendOnEnter", sendOnEnter );
    c->writeEntry( "UseWallet", useWallet );
    c->writeEntry( "SystemPrompt", systemPrompt );

    c->setGroup( "AI Horde" );
    c->writeEntry( "Url", hordeUrl );
    c->writeEntry( "PromptFormat", hordePromptFormat );
    c->writeEntry( "AllowNsfw", hordeNsfw );
    c->writeEntry( "TrustedWorkersOnly", hordeTrustedOnly );
    c->writeEntry( "SlowWorkers", hordeSlowWorkers );
    if ( inWallet )
        c->deleteEntry( "ApiKey" );
    else
        c->writeEntry( "ApiKey", hordeKey );

    c->setGroup( "KoboldCpp" );
    c->writeEntry( "Url", koboldUrl );
    c->writeEntry( "Voice", koboldVoice );
    if ( inWallet )
        c->deleteEntry( "ApiKey" );
    else
        c->writeEntry( "ApiKey", koboldKey );

    c->setGroup( "OpenAI" );
    c->writeEntry( "Url", openaiUrl );
    c->writeEntry( "TranscribeAudioFirst", openaiTranscribeFirst );
    c->writeEntry( "TranscribeModel", openaiTranscribeModel );
    c->writeEntry( "SpeechModel", openaiSpeechModel );
    c->writeEntry( "Voice", openaiVoice );
    if ( inWallet )
        c->deleteEntry( "ApiKey" );
    else
        c->writeEntry( "ApiKey", openaiKey );

    c->setGroup( "Generation" );
    c->writeEntry( "MaxTokens", maxTokens );
    c->writeEntry( "ContextLength", contextLength );
    c->writeEntry( "Temperature", temperature );
    c->writeEntry( "TopP", topP );
    c->writeEntry( "ImageWidth", imageWidth );
    c->writeEntry( "ImageHeight", imageHeight );
    c->writeEntry( "ImageSteps", imageSteps );
    c->writeEntry( "ImageCfgScale", imageCfgScale );
    c->writeEntry( "ImageDenoise", imageDenoise );
    c->writeEntry( "NegativePrompt", negativePrompt );

    c->sync();

    protectFile();
    return inWallet == wanted;
}

/* The configuration file may hold API keys: keep it private. */
void Settings::protectFile()
{
    const TQString file = locateLocal( "config", "tde-ai-clientrc" );
    if ( TQFile::exists( file ) )
        ::chmod( TQFile::encodeName( file ), 0600 );
}

TQString Settings::model( const TQString &backend, int mode ) const
{
    TDEConfig *c = TDEGlobal::config();
    c->setGroup( "Models" );
    return c->readEntry( TQString( "%1_%2" ).arg( backend ).arg( mode ) );
}

void Settings::setModel( const TQString &backend, int mode, const TQString &model )
{
    TDEConfig *c = TDEGlobal::config();
    c->setGroup( "Models" );
    c->writeEntry( TQString( "%1_%2" ).arg( backend ).arg( mode ), model );
}

static TDEWallet::Wallet *openWallet()
{
    WId window = 0;
    if ( kapp && kapp->mainWidget() )
        window = kapp->mainWidget()->winId();
    TDEWallet::Wallet *w = TDEWallet::Wallet::openWallet( TDEWallet::Wallet::NetworkWallet(), window );
    if ( !w )
        return 0;
    if ( !w->hasFolder( walletFolder ) && !w->createFolder( walletFolder ) ) {
        delete w;
        return 0;
    }
    w->setFolder( walletFolder );
    return w;
}

bool Settings::readWallet()
{
    TDEWallet::Wallet *w = openWallet();
    if ( !w )
        return false;
    TQString v;
    if ( w->readPassword( "horde", v ) == 0 && !v.isEmpty() )
        hordeKey = v;
    if ( w->readPassword( "koboldcpp", v ) == 0 )
        koboldKey = v;
    if ( w->readPassword( "openai", v ) == 0 )
        openaiKey = v;
    delete w;
    return true;
}

bool Settings::writeWallet()
{
    TDEWallet::Wallet *w = openWallet();
    if ( !w )
        return false;
    bool ok = w->writePassword( "horde", hordeKey ) == 0;
    ok = w->writePassword( "koboldcpp", koboldKey ) == 0 && ok;
    ok = w->writePassword( "openai", openaiKey ) == 0 && ok;
    delete w;
    return ok;
}
