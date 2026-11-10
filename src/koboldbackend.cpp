/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "koboldbackend.h"
#include "httpclient.h"
#include "json.h"
#include "settings.h"

#include <string.h>

#include <tdelocale.h>

KoboldBackend::KoboldBackend( TQObject *parent )
    : OpenAIBackend( parent, "koboldcpp backend" ), m_caps( CapChat ), m_nativeAudio( false ), m_probed( false )
{
}

TQString KoboldBackend::name() const
{
    return i18n( "KoboldCpp" );
}

TQString KoboldBackend::address() const
{
    TQString u = trimmedUrl( Settings::self().koboldUrl );
    if ( !u.isEmpty() && !u.contains( "://" ) )
        u.prepend( "http://" );
    /* people paste the address of one of the APIs: go back to the server itself */
    static const char *const suffixes[] = { "/v1", "/api/v1", "/api", 0 };
    for ( int i = 0; suffixes[i]; ++i )
        if ( u.endsWith( suffixes[i] ) )
            u.truncate( u.length() - strlen( suffixes[i] ) );
    return u;
}

TQString KoboldBackend::apiKey() const
{
    return Settings::self().koboldKey.stripWhiteSpace();
}

TQString KoboldBackend::chatUrl() const
{
    return address() + "/v1/chat/completions";
}

void KoboldBackend::refresh()
{
    HttpRequest *r = HttpRequest::get( address() + "/api/extra/version", headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotVersion( HttpRequest * ) ) );
    r = HttpRequest::get( address() + "/api/v1/model", headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotModel( HttpRequest * ) ) );
}

void KoboldBackend::slotVersion( HttpRequest *r )
{
    m_probed = true;
    if ( !r->ok() ) {
        m_caps = CapChat;
        emit capabilitiesChanged();
        emit accountInfo( TQString() );
        emit serviceMessage( i18n( "KoboldCpp could not be reached at %1: %2" ).arg( address() ).arg( r->errorString() ) );
        return;
    }
    JsonValue v = r->json();
    int caps = CapChat;
    if ( v.get( "vision" ).toBool() )
        caps |= CapImageInput;
    m_nativeAudio = v.get( "audio" ).toBool();
    if ( m_nativeAudio || v.get( "transcribe" ).toBool() )
        caps |= CapAudioInput;
    if ( v.get( "txt2img" ).toBool() )
        caps |= CapImageGen | CapImageToImage;
    if ( v.get( "tts" ).toBool() )
        caps |= CapSpeech;
    m_caps = caps;
    emit capabilitiesChanged();
    emit accountInfo( i18n( "%1 %2" ).arg( v.get( "result" ).toString() ).arg( v.get( "version" ).toString() ) );

    if ( caps & CapImageGen ) {
        HttpRequest *m = HttpRequest::get( address() + "/sdapi/v1/sd-models", headers(), this );
        connect( m, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageModels( HttpRequest * ) ) );
    } else {
        setModels( Request::Image, TQStringList() );
    }
}

void KoboldBackend::slotModel( HttpRequest *r )
{
    TQStringList names;
    if ( r->ok() ) {
        TQString model = r->json().get( "result" ).toString();
        if ( model.startsWith( "koboldcpp/" ) )
            model = model.mid( 10 );
        if ( !model.isEmpty() )
            names << model;
    }
    setModels( Request::Chat, names );
}

void KoboldBackend::slotImageModels( HttpRequest *r )
{
    TQStringList names;
    if ( r->ok() ) {
        JsonValue list = r->json();
        for ( uint i = 0; i < list.count(); ++i ) {
            TQString n = list.at( i ).get( "title" ).toString();
            if ( n.isEmpty() )
                n = list.at( i ).get( "model_name" ).toString();
            if ( !n.isEmpty() )
                names << n;
        }
    }
    setModels( Request::Image, names );
}

void KoboldBackend::stop()
{
    /* closing the connection is not enough: tell KoboldCpp to stop generating */
    if ( busy() )
        HttpRequest::postJson( address() + "/api/extra/abort", JsonValue::object(), headers(), this );
}

void KoboldBackend::startTranscription( const Attachment &audio )
{
    JsonValue body = JsonValue::object();
    body.set( "audio_data", audio.dataUri() );
    body.set( "prompt", "" );
    body.set( "langcode", "auto" );
    HttpRequest *r = track( HttpRequest::postJson( address() + "/api/extra/transcribe", body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotKoboldTranscribed( HttpRequest * ) ) );
}

void KoboldBackend::slotKoboldTranscribed( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( i18n( "The audio file could not be transcribed: %1" ).arg( r->errorString() ) );
        return;
    }
    transcriptReady( r->json().get( "text" ).toString() );
}

void KoboldBackend::runImage( const Request &request )
{
    const Settings &s = Settings::self();

    JsonValue body = JsonValue::object();
    body.set( "prompt", request.prompt );
    body.set( "negative_prompt", s.negativePrompt );
    body.set( "width", imageDimension( s.imageWidth ) );
    body.set( "height", imageDimension( s.imageHeight ) );
    body.set( "steps", s.imageSteps );
    body.set( "cfg_scale", s.imageCfgScale );
    body.set( "sampler_name", "Euler a" );

    TQString path = "/sdapi/v1/txt2img";
    for ( AttachmentList::ConstIterator a = request.attachments.begin(); a != request.attachments.end(); ++a ) {
        if ( ( *a ).kind != Attachment::Image )
            continue;
        JsonValue init = JsonValue::array();
        init.append( ( *a ).base64() );
        body.set( "init_images", init );
        body.set( "denoising_strength", s.imageDenoise );
        path = "/sdapi/v1/img2img";
        break;
    }

    emit status( i18n( "KoboldCpp is generating the image..." ) );
    HttpRequest *r = track( HttpRequest::postJson( address() + path, body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageResult( HttpRequest * ) ) );
}

void KoboldBackend::runSpeech( const Request &request )
{
    JsonValue body = JsonValue::object();
    body.set( "input", request.prompt );
    body.set( "voice", Settings::self().koboldVoice );

    emit status( i18n( "KoboldCpp is generating the audio..." ) );
    HttpRequest *r = track( HttpRequest::postJson( address() + "/api/extra/tts", body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotSpeechResult( HttpRequest * ) ) );
}

#include "koboldbackend.moc"
