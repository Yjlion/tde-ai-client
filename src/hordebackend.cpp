/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "hordebackend.h"
#include "httpclient.h"
#include "json.h"
#include "settings.h"

#include <tqfile.h>
#include <tqtimer.h>

#include <tdelocale.h>
#include <tdeglobal.h>
#include <kmdcodec.h>
#include <kurl.h>

static const int pollInterval = 2500;   // ms

HordeBackend::HordeBackend( TQObject *parent )
    : Backend( parent, "horde backend" ), m_job( NoJob )
{
    m_pollTimer = new TQTimer( this );
    connect( m_pollTimer, TQ_SIGNAL( timeout() ), this, TQ_SLOT( slotPoll() ) );
}

TQString HordeBackend::name() const
{
    return i18n( "AI Horde" );
}

TQString HordeBackend::address() const
{
    TQString u = Settings::self().hordeUrl.stripWhiteSpace();
    while ( u.endsWith( "/" ) )
        u.truncate( u.length() - 1 );
    return u;
}

int HordeBackend::capabilities() const
{
    /* Images attached to a chat are described by the Horde's "interrogation"
       workers; the description is what the text model gets to see. */
    return CapChat | CapImageInput | CapImageGen | CapImageToImage;
}

TQString HordeBackend::url( const TQString &path ) const
{
    return address() + path;
}

TQStringList HordeBackend::headers() const
{
    TQString key = Settings::self().hordeKey.stripWhiteSpace();
    if ( key.isEmpty() )
        key = Settings::anonymousHordeKey();
    TQStringList h;
    h << "apikey: " + key
      << "Client-Agent: tde-ai-client:" VERSION ":https://github.com/Yjlion/tde-ai-client";
    return h;
}

/* -------------------------------------------------------------- service */

void HordeBackend::refresh()
{
    /* not tracked: cancelling a request must not stop these */
    HttpRequest *r = HttpRequest::get( url( "/status/models?type=text" ), headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotTextModels( HttpRequest * ) ) );
    r = HttpRequest::get( url( "/status/models?type=image" ), headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageModels( HttpRequest * ) ) );
    r = HttpRequest::get( url( "/find_user" ), headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotUser( HttpRequest * ) ) );
}

/* Models with the most workers first: those answer soonest. */
static TQStringList modelNames( const JsonValue &list )
{
    TQMap<TQString, TQString> sorted;
    for ( uint i = 0; i < list.count(); ++i ) {
        const JsonValue &m = list.at( i );
        const TQString name = m.get( "name" ).toString();
        if ( name.isEmpty() )
            continue;
        TQString key;
        key.sprintf( "%06d", 999999 - TQMIN( 999999, m.get( "count" ).toInt() ) );
        sorted.insert( key + name.lower(), name );
    }
    return sorted.values();
}

void HordeBackend::slotTextModels( HttpRequest *r )
{
    if ( !r->ok() ) {
        emit serviceMessage( i18n( "The list of text models could not be fetched from %1: %2" )
                   .arg( address() ).arg( r->errorString() ) );
        return;
    }
    setModels( Request::Chat, modelNames( r->json() ) );
}

void HordeBackend::slotImageModels( HttpRequest *r )
{
    if ( r->ok() )
        setModels( Request::Image, modelNames( r->json() ) );
}

void HordeBackend::slotUser( HttpRequest *r )
{
    if ( !r->ok() ) {
        emit accountInfo( TQString() );
        return;
    }
    JsonValue u = r->json();
    emit accountInfo( i18n( "%1 - %2 kudos" ).arg( u.get( "username" ).toString() )
                      .arg( TDEGlobal::locale()->formatNumber( u.get( "kudos" ).toDouble(), 0 ) ) );
}

/* -------------------------------------------------------------- requests */

void HordeBackend::run( const Request &request )
{
    m_request = request;
    m_toCaption.clear();
    m_job = NoJob;
    m_jobId = TQString();

    if ( request.mode == Request::Chat ) {
        for ( MessageList::ConstIterator m = request.history.begin(); m != request.history.end(); ++m ) {
            if ( ( *m ).role != ChatMessage::User )
                continue;
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a )
                if ( ( *a ).kind == Attachment::Image && !m_derived.contains( ( *a ).path )
                     && TQFile::exists( ( *a ).path ) )
                    m_toCaption.append( *a );
        }
    }
    nextStep();
}

void HordeBackend::nextStep()
{
    if ( !m_toCaption.isEmpty() ) {
        Attachment a = m_toCaption.first();
        m_toCaption.remove( m_toCaption.begin() );
        submitCaption( a );
    } else if ( m_request.mode == Request::Image ) {
        submitImage();
    } else {
        submitText();
    }
}

void HordeBackend::stop()
{
    m_pollTimer->stop();
    if ( !m_jobId.isEmpty() ) {
        /* tell the Horde, so no worker wastes time on it */
        const char *path = m_job == TextJob ? "/generate/text/status/"
                         : m_job == ImageJob ? "/generate/status/" : "/interrogate/status/";
        HttpRequest::del( url( path + m_jobId ), headers(), this );
    }
    m_jobId = TQString();
    m_job = NoJob;
}

TQString HordeBackend::buildPrompt( const MessageList &history, const TQMap<TQString, TQString> &captions,
                                    TQStringList &stops )
{
    const Settings &s = Settings::self();
    const int format = s.hordePromptFormat;
    const TQString system = s.systemPrompt.stripWhiteSpace();
    TQString p;

    if ( format == Settings::FormatChatML ) {
        if ( !system.isEmpty() )
            p += "<|im_start|>system\n" + system + "<|im_end|>\n";
        stops << "<|im_end|>" << "<|im_start|>";
    } else {
        if ( !system.isEmpty() )
            p += system + "\n\n";
        if ( format == Settings::FormatAlpaca )
            stops << "### Instruction:" << "### Response:";
        else
            stops << "\nUser:" << "\nAssistant:";
    }

    for ( MessageList::ConstIterator m = history.begin(); m != history.end(); ++m ) {
        const bool user = ( *m ).role == ChatMessage::User;
        if ( !user && ( *m ).role != ChatMessage::Assistant )
            continue;
        TQString text = user ? textWithFiles( *m ) : ( *m ).text.stripWhiteSpace();
        if ( user ) {
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a )
                if ( ( *a ).kind == Attachment::Image && captions.contains( ( *a ).path ) )
                    text += "\n\n" + i18n( "[Attached image \"%1\". It shows: %2]" )
                                     .arg( ( *a ).name() ).arg( captions[( *a ).path] );
            text = text.stripWhiteSpace();
        }
        if ( text.isEmpty() )
            continue;
        switch ( format ) {
        case Settings::FormatChatML:
            p += TQString( "<|im_start|>%1\n" ).arg( user ? "user" : "assistant" ) + text + "<|im_end|>\n";
            break;
        case Settings::FormatAlpaca:
            p += ( user ? "### Instruction:\n" : "### Response:\n" ) + text + "\n\n";
            break;
        default:
            p += ( user ? "User: " : "Assistant: " ) + text + "\n";
        }
    }

    switch ( format ) {
    case Settings::FormatChatML: p += "<|im_start|>assistant\n"; break;
    case Settings::FormatAlpaca: p += "### Response:\n"; break;
    default:                     p += "Assistant:";
    }
    return p;
}

void HordeBackend::submitText()
{
    const Settings &s = Settings::self();
    TQStringList stops;
    const TQString prompt = buildPrompt( m_request.history, m_derived, stops );

    JsonValue params = JsonValue::object();
    params.set( "n", 1 );
    params.set( "max_length", TQMAX( 16, TQMIN( s.maxTokens, 1024 ) ) );
    params.set( "max_context_length", TQMAX( 512, s.contextLength ) );
    params.set( "temperature", s.temperature );
    params.set( "top_p", s.topP );
    JsonValue stop = JsonValue::array();
    for ( TQStringList::ConstIterator it = stops.begin(); it != stops.end(); ++it )
        stop.append( *it );
    params.set( "stop_sequence", stop );

    JsonValue body = JsonValue::object();
    body.set( "prompt", prompt );
    body.set( "params", params );
    body.set( "trusted_workers", s.hordeTrustedOnly );
    body.set( "slow_workers", s.hordeSlowWorkers );
    if ( !m_request.model.isEmpty() ) {
        JsonValue models = JsonValue::array();
        models.append( m_request.model );
        body.set( "models", models );
    }

    m_job = TextJob;
    emit status( i18n( "Sending the request to the AI Horde..." ) );
    HttpRequest *r = track( HttpRequest::postJson( url( "/generate/text/async" ), body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotSubmitted( HttpRequest * ) ) );
}

void HordeBackend::submitImage()
{
    const Settings &s = Settings::self();

    TQString prompt = m_request.prompt.stripWhiteSpace();
    if ( !s.negativePrompt.stripWhiteSpace().isEmpty() )
        prompt += " ### " + s.negativePrompt.stripWhiteSpace();

    JsonValue params = JsonValue::object();
    params.set( "n", 1 );
    params.set( "width", imageDimension( s.imageWidth ) );
    params.set( "height", imageDimension( s.imageHeight ) );
    params.set( "steps", s.imageSteps );
    params.set( "cfg_scale", s.imageCfgScale );
    params.set( "sampler_name", "k_euler_a" );

    JsonValue body = JsonValue::object();
    body.set( "prompt", prompt );
    body.set( "nsfw", s.hordeNsfw );
    body.set( "censor_nsfw", !s.hordeNsfw );
    body.set( "trusted_workers", s.hordeTrustedOnly );
    body.set( "slow_workers", s.hordeSlowWorkers );
    body.set( "r2", true );
    if ( !m_request.model.isEmpty() ) {
        JsonValue models = JsonValue::array();
        models.append( m_request.model );
        body.set( "models", models );
    }
    for ( AttachmentList::ConstIterator a = m_request.attachments.begin(); a != m_request.attachments.end(); ++a ) {
        if ( ( *a ).kind != Attachment::Image )
            continue;
        body.set( "source_image", ( *a ).base64() );
        body.set( "source_processing", "img2img" );
        params.set( "denoising_strength", s.imageDenoise );
        break;
    }
    body.set( "params", params );

    m_job = ImageJob;
    emit status( i18n( "Sending the request to the AI Horde..." ) );
    HttpRequest *r = track( HttpRequest::postJson( url( "/generate/async" ), body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotSubmitted( HttpRequest * ) ) );
}

void HordeBackend::submitCaption( const Attachment &image )
{
    JsonValue form = JsonValue::object();
    form.set( "name", "caption" );
    JsonValue forms = JsonValue::array();
    forms.append( form );
    JsonValue body = JsonValue::object();
    body.set( "forms", forms );
    body.set( "source_image", image.base64() );
    body.set( "slow_workers", Settings::self().hordeSlowWorkers );

    m_job = CaptionJob;
    m_captionPath = image.path;
    emit status( i18n( "Asking the AI Horde to describe %1..." ).arg( image.name() ) );
    HttpRequest *r = track( HttpRequest::postJson( url( "/interrogate/async" ), body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotSubmitted( HttpRequest * ) ) );
}

void HordeBackend::slotSubmitted( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( r->errorString() );
        return;
    }
    JsonValue v = r->json();
    m_jobId = v.get( "id" ).toString();
    if ( m_jobId.isEmpty() ) {
        fail( i18n( "The AI Horde did not accept the request: %1" ).arg( r->errorString() ) );
        return;
    }
    const TQString warning = v.get( "message" ).toString();
    if ( !warning.isEmpty() )
        emit note( warning );
    emit status( i18n( "The request is waiting in the queue of the AI Horde..." ) );
    m_pollTimer->start( pollInterval, true );
}

void HordeBackend::slotPoll()
{
    if ( !busy() || m_jobId.isEmpty() )
        return;
    const char *path = m_job == TextJob ? "/generate/text/status/"
                     : m_job == ImageJob ? "/generate/check/" : "/interrogate/status/";
    HttpRequest *r = track( HttpRequest::get( url( path + m_jobId ), headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotStatus( HttpRequest * ) ) );
}

void HordeBackend::queueStatus( const TQString &what, const JsonValue &state )
{
    if ( state.get( "processing" ).toInt() > 0 ) {
        emit status( i18n( "%1: a worker is on it, about %2 s left" ).arg( what ).arg( state.get( "wait_time" ).toInt() ) );
    } else if ( state.has( "queue_position" ) ) {
        emit status( i18n( "%1: waiting in the queue at position %2, about %3 s left" ).arg( what )
                     .arg( state.get( "queue_position" ).toInt() ).arg( state.get( "wait_time" ).toInt() ) );
    } else {
        emit status( i18n( "%1: waiting for a worker..." ).arg( what ) );
    }
}

void HordeBackend::slotStatus( HttpRequest *r )
{
    if ( !busy() || m_jobId.isEmpty() )
        return;
    if ( !r->ok() ) {
        if ( r->status() == 429 ) {     // polled too often: just wait longer
            m_pollTimer->start( pollInterval * 3, true );
            return;
        }
        fail( r->errorString() );
        return;
    }
    JsonValue v = r->json();

    if ( m_job == CaptionJob ) {
        const TQString state = v.get( "state" ).toString();
        if ( state == "done" ) {
            TQString caption;
            const JsonValue &forms = v.get( "forms" );
            for ( uint i = 0; i < forms.count(); ++i ) {
                const JsonValue &result = forms.at( i ).get( "result" );
                if ( result.has( "caption" ) )
                    caption = result.get( "caption" ).toString();
            }
            m_derived.insert( m_captionPath, caption.isEmpty() ? i18n( "(no description available)" ) : caption );
            m_jobId = TQString();
            nextStep();
        } else if ( state == "faulted" || state == "cancelled" || state == "partial" ) {
            fail( i18n( "The AI Horde could not describe the attached image." ) );
        } else {
            emit status( i18n( "Waiting for a worker to describe the attached image..." ) );
            m_pollTimer->start( pollInterval, true );
        }
        return;
    }

    if ( v.get( "faulted" ).toBool() ) {
        fail( i18n( "The AI Horde worker failed to do the request." ) );
        return;
    }
    if ( v.has( "is_possible" ) && !v.get( "is_possible" ).toBool() ) {
        fail( i18n( "No AI Horde worker can do this request at the moment. "
                    "Choose another model, or lower the answer length or image size." ) );
        return;
    }
    if ( !v.get( "done" ).toBool() ) {
        queueStatus( m_job == TextJob ? i18n( "Text" ) : i18n( "Image" ), v );
        m_pollTimer->start( pollInterval, true );
        return;
    }

    if ( m_job == ImageJob ) {
        emit status( i18n( "Fetching the image..." ) );
        HttpRequest *s = track( HttpRequest::get( url( "/generate/status/" + m_jobId ), headers(), this ) );
        connect( s, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageResult( HttpRequest * ) ) );
        m_jobId = TQString();
        return;
    }

    m_jobId = TQString();
    const JsonValue &gen = v.get( "generations" ).at( 0 );
    TQString text = gen.get( "text" ).toString();
    if ( gen.isNull() ) {
        fail( i18n( "The AI Horde returned no text." ) );
        return;
    }
    /* cut what the model wrote beyond its own turn */
    TQStringList stops;
    buildPrompt( MessageList(), m_derived, stops );
    for ( TQStringList::ConstIterator it = stops.begin(); it != stops.end(); ++it ) {
        int pos = text.find( *it );
        if ( pos >= 0 )
            text.truncate( pos );
    }
    emit meta( gen.get( "model" ).toString() );
    emit textDelta( text.stripWhiteSpace() );
    done();
}

void HordeBackend::slotImageResult( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( r->errorString() );
        return;
    }
    const JsonValue result = r->json();
    const JsonValue &gen = result.get( "generations" ).at( 0 );
    const TQString img = gen.get( "img" ).toString();
    if ( img.isEmpty() ) {
        fail( i18n( "The AI Horde returned no image." ) );
        return;
    }
    emit meta( gen.get( "model" ).toString() );
    if ( gen.get( "censored" ).toBool() )
        emit note( i18n( "The worker's safety filter replaced the image." ) );

    if ( img.startsWith( "http://" ) || img.startsWith( "https://" ) ) {
        HttpRequest *d = track( HttpRequest::get( img, TQStringList(), this ) );
        connect( d, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageDownloaded( HttpRequest * ) ) );
        return;
    }
    TQByteArray data;
    KCodecs::base64Decode( TQCString( img.latin1() ), data );
    emit media( saveMedia( data, TQString() ) );
    done();
}

void HordeBackend::slotImageDownloaded( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() || r->body().isEmpty() ) {
        fail( i18n( "The image could not be downloaded: %1" ).arg( r->errorString() ) );
        return;
    }
    emit media( saveMedia( r->body(), TQString() ) );
    done();
}

#include "hordebackend.moc"
