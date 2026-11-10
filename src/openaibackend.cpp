/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "openaibackend.h"
#include "httpclient.h"
#include "json.h"
#include "settings.h"

#include <string.h>

#include <tqfile.h>

#include <tdeapplication.h>
#include <tdelocale.h>
#include <kmdcodec.h>

OpenAIBackend::OpenAIBackend( TQObject *parent, const char *name )
    : Backend( parent, name ), m_gotText( false ), m_gotMeta( false )
{
}

TQString OpenAIBackend::name() const
{
    return i18n( "OpenAI-compatible" );
}

TQString OpenAIBackend::trimmedUrl( const TQString &url )
{
    TQString u = url.stripWhiteSpace();
    while ( u.endsWith( "/" ) )
        u.truncate( u.length() - 1 );
    return u;
}

TQString OpenAIBackend::address() const
{
    return trimmedUrl( Settings::self().openaiUrl );
}

TQString OpenAIBackend::apiKey() const
{
    return Settings::self().openaiKey.stripWhiteSpace();
}

TQString OpenAIBackend::chatUrl() const
{
    return address() + "/chat/completions";
}

bool OpenAIBackend::transcribeAudio() const
{
    return Settings::self().openaiTranscribeFirst;
}

TQString OpenAIBackend::defaultChatModel() const
{
    const TQStringList known = models( Request::Chat );
    return known.isEmpty() ? TQString() : known.first();
}

int OpenAIBackend::capabilities() const
{
    /* There is no way to ask such a server what it can do, so everything
       the API knows is offered; the server refuses what it cannot handle. */
    return CapChat | CapImageInput | CapAudioInput | CapVideoInput | CapFileInput
           | CapImageGen | CapSpeech;
}

TQStringList OpenAIBackend::headers() const
{
    TQStringList h;
    const TQString key = apiKey();
    if ( !key.isEmpty() )
        h << "Authorization: Bearer " + key;
    return h;
}

void OpenAIBackend::refresh()
{
    HttpRequest *r = HttpRequest::get( address() + "/models", headers(), this );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotModels( HttpRequest * ) ) );
}

void OpenAIBackend::slotModels( HttpRequest *r )
{
    if ( !r->ok() ) {
        emit serviceMessage( i18n( "The list of models could not be fetched from %1: %2" )
                   .arg( address() ).arg( r->errorString() ) );
        return;
    }
    JsonValue v = r->json();
    const JsonValue &list = v.isArray() ? v : v.get( "data" );
    TQStringList names;
    for ( uint i = 0; i < list.count(); ++i ) {
        const TQString id = list.at( i ).get( "id" ).toString();
        if ( !id.isEmpty() )
            names << id;
    }
    names.sort();
    /* the server does not say which model does what */
    setModels( Request::Chat, names );
    setModels( Request::Image, names );
    setModels( Request::Speech, names );
}

/* ----------------------------------------------------------------- chat */

void OpenAIBackend::run( const Request &request )
{
    m_request = request;
    m_toTranscribe.clear();

    if ( request.mode == Request::Image ) {
        runImage( request );
        return;
    }
    if ( request.mode == Request::Speech ) {
        runSpeech( request );
        return;
    }

    if ( transcribeAudio() ) {
        for ( MessageList::ConstIterator m = request.history.begin(); m != request.history.end(); ++m ) {
            if ( ( *m ).role != ChatMessage::User )
                continue;
            for ( AttachmentList::ConstIterator a = ( *m ).attachments.begin(); a != ( *m ).attachments.end(); ++a )
                if ( ( *a ).kind == Attachment::Audio && !m_derived.contains( ( *a ).path )
                     && TQFile::exists( ( *a ).path ) )
                    m_toTranscribe.append( *a );
        }
    }
    nextStep();
}

void OpenAIBackend::stop()
{
}

void OpenAIBackend::nextStep()
{
    if ( m_toTranscribe.isEmpty() ) {
        sendChat();
        return;
    }
    Attachment a = m_toTranscribe.first();
    m_toTranscribe.remove( m_toTranscribe.begin() );
    m_transcribing = a.path;
    emit status( i18n( "Transcribing %1..." ).arg( a.name() ) );
    startTranscription( a );
}

void OpenAIBackend::startTranscription( const Attachment &audio )
{
    const TQCString boundary = TQCString( "----tde-ai-client-" ) + TDEApplication::randomString( 24 ).latin1();
    const TQByteArray file = audio.data();

    TQCString head;
    head += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n";
    head += Settings::self().openaiTranscribeModel.utf8() + "\r\n";
    head += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"";
    head += audio.name().replace( '"', '_' ).utf8();
    head += "\"\r\nContent-Type: ";
    head += audio.mime.latin1();
    head += "\r\n\r\n";
    const TQCString tail = "\r\n--" + boundary + "--\r\n";

    TQByteArray body( head.length() + file.size() + tail.length() );
    memcpy( body.data(), head.data(), head.length() );
    memcpy( body.data() + head.length(), file.data(), file.size() );
    memcpy( body.data() + head.length() + file.size(), tail.data(), tail.length() );

    HttpRequest *r = track( HttpRequest::post( address() + "/audio/transcriptions", body,
                                               TQString( "multipart/form-data; boundary=" ) + boundary.data(), headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotTranscribed( HttpRequest * ) ) );
}

void OpenAIBackend::slotTranscribed( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( i18n( "The audio file could not be transcribed: %1" ).arg( r->errorString() ) );
        return;
    }
    transcriptReady( r->json().get( "text" ).toString() );
}

void OpenAIBackend::transcriptReady( const TQString &text )
{
    m_derived.insert( m_transcribing, text.stripWhiteSpace() );
    nextStep();
}

static TQString audioFormat( const Attachment &a )
{
    if ( a.mime.contains( "mpeg" ) || a.mime.contains( "mp3" ) )
        return "mp3";
    if ( a.mime.contains( "wav" ) )
        return "wav";
    const int dot = a.path.findRev( '.' );
    return dot >= 0 ? a.path.mid( dot + 1 ).lower() : TQString( "wav" );
}

JsonValue OpenAIBackend::messageContent( const ChatMessage &message ) const
{
    TQString text = textWithFiles( message );
    JsonValue parts = JsonValue::array();

    for ( AttachmentList::ConstIterator a = message.attachments.begin(); a != message.attachments.end(); ++a ) {
        const Attachment &att = *a;
        if ( att.kind == Attachment::Text || !accepts( Request::Chat, att.kind ) || !TQFile::exists( att.path ) )
            continue;

        JsonValue part = JsonValue::object();
        if ( att.kind == Attachment::Audio && m_derived.contains( att.path ) ) {
            text += "\n\n" + i18n( "[Transcript of the audio file \"%1\": %2]" ).arg( att.name() ).arg( m_derived[att.path] );
            continue;
        } else if ( att.kind == Attachment::Image ) {
            JsonValue url = JsonValue::object();
            url.set( "url", att.dataUri() );
            part.set( "type", "image_url" );
            part.set( "image_url", url );
        } else if ( att.kind == Attachment::Audio ) {
            JsonValue audio = JsonValue::object();
            audio.set( "data", att.base64() );
            audio.set( "format", audioFormat( att ) );
            part.set( "type", "input_audio" );
            part.set( "input_audio", audio );
        } else if ( att.kind == Attachment::Video ) {
            JsonValue url = JsonValue::object();
            url.set( "url", att.dataUri() );
            part.set( "type", "video_url" );
            part.set( "video_url", url );
        } else {
            JsonValue file = JsonValue::object();
            file.set( "filename", att.name() );
            file.set( "file_data", att.dataUri() );
            part.set( "type", "file" );
            part.set( "file", file );
        }
        parts.append( part );
    }

    text = text.stripWhiteSpace();
    if ( parts.count() == 0 )
        return JsonValue( text );   // the plain form every server understands

    JsonValue all = JsonValue::array();
    if ( !text.isEmpty() ) {
        JsonValue t = JsonValue::object();
        t.set( "type", "text" );
        t.set( "text", text );
        all.append( t );
    }
    for ( uint i = 0; i < parts.count(); ++i )
        all.append( parts.at( i ) );
    return all;
}

void OpenAIBackend::sendChat()
{
    const Settings &s = Settings::self();

    JsonValue messages = JsonValue::array();
    if ( !s.systemPrompt.stripWhiteSpace().isEmpty() ) {
        JsonValue sys = JsonValue::object();
        sys.set( "role", "system" );
        sys.set( "content", s.systemPrompt.stripWhiteSpace() );
        messages.append( sys );
    }
    for ( MessageList::ConstIterator m = m_request.history.begin(); m != m_request.history.end(); ++m ) {
        JsonValue msg = JsonValue::object();
        if ( ( *m ).role == ChatMessage::User ) {
            msg.set( "role", "user" );
            msg.set( "content", messageContent( *m ) );
        } else if ( ( *m ).role == ChatMessage::Assistant && !( *m ).text.stripWhiteSpace().isEmpty() ) {
            msg.set( "role", "assistant" );
            msg.set( "content", ( *m ).text );
        } else {
            continue;
        }
        messages.append( msg );
    }

    JsonValue body = JsonValue::object();
    const TQString model = m_request.model.isEmpty() ? defaultChatModel() : m_request.model;
    if ( !model.isEmpty() )
        body.set( "model", model );
    body.set( "messages", messages );
    body.set( "stream", true );
    body.set( "max_tokens", s.maxTokens );
    body.set( "temperature", s.temperature );
    body.set( "top_p", s.topP );

    m_sse = TQCString();
    m_gotText = false;
    m_gotMeta = false;
    m_streamError = TQString();
    emit status( i18n( "Waiting for %1 to answer..." ).arg( name() ) );

    HttpRequest *r = track( HttpRequest::postJson( chatUrl(), body, headers(), this ) );
    connect( r, TQ_SIGNAL( chunk( const TQByteArray & ) ), this, TQ_SLOT( slotChatData( const TQByteArray & ) ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotChatFinished( HttpRequest * ) ) );
}

void OpenAIBackend::slotChatData( const TQByteArray &data )
{
    if ( !busy() )
        return;
    m_sse += TQCString( data.data(), data.size() + 1 );

    int nl;
    while ( ( nl = m_sse.find( '\n' ) ) >= 0 ) {
        TQCString line = m_sse.left( nl );
        m_sse = m_sse.mid( nl + 1 );
        if ( line.length() && line[(int) line.length() - 1] == '\r' )
            line.truncate( line.length() - 1 );
        if ( line.left( 5 ) != "data:" )
            continue;
        handleEvent( line.mid( 5 ).stripWhiteSpace() );
    }
}

void OpenAIBackend::handleEvent( const TQCString &payload )
{
    if ( payload.isEmpty() || payload == "[DONE]" )
        return;
    bool ok = false;
    JsonValue v = JsonValue::parse( TQString::fromUtf8( payload ), &ok );
    if ( !ok )
        return;

    if ( v.has( "error" ) ) {
        const JsonValue &e = v.get( "error" );
        m_streamError = e.isObject() ? e.get( "message" ).toString() : e.toString();
        return;
    }
    if ( !m_gotMeta && !v.get( "model" ).toString().isEmpty() ) {
        m_gotMeta = true;
        emit meta( v.get( "model" ).toString() );
    }
    const JsonValue &choice = v.get( "choices" ).at( 0 );
    TQString text = choice.get( "delta" ).get( "content" ).toString();
    if ( text.isEmpty() && choice.has( "text" ) )
        text = choice.get( "text" ).toString();     // old style completion chunks
    if ( !text.isEmpty() ) {
        if ( !m_gotText )
            emit status( i18n( "%1 is answering..." ).arg( name() ) );
        m_gotText = true;
        emit textDelta( text );
    }
}

void OpenAIBackend::slotChatFinished( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( r->errorString() );
        return;
    }
    if ( !m_sse.isEmpty() && m_sse.left( 5 ) == "data:" )       // last line without newline
        handleEvent( m_sse.mid( 5 ).stripWhiteSpace() );

    if ( !m_streamError.isEmpty() ) {
        fail( m_streamError );
        return;
    }
    if ( !m_gotText ) {
        /* a server that ignored "stream": the whole answer is one document */
        bool ok = false;
        JsonValue v = JsonValue::parse( r->body(), &ok );
        if ( ok && v.isObject() ) {
            if ( v.has( "error" ) ) {
                fail( r->errorString() );
                return;
            }
            const TQString text = v.get( "choices" ).at( 0 ).get( "message" ).get( "content" ).toString();
            if ( !text.isEmpty() ) {
                if ( !v.get( "model" ).toString().isEmpty() )
                    emit meta( v.get( "model" ).toString() );
                emit textDelta( text );
                m_gotText = true;
            }
        }
    }
    if ( !m_gotText ) {
        fail( i18n( "The service sent an empty answer." ) );
        return;
    }
    done();
}

/* ---------------------------------------------------------------- image */

void OpenAIBackend::runImage( const Request &request )
{
    const Settings &s = Settings::self();
    JsonValue body = JsonValue::object();
    body.set( "prompt", request.prompt );
    if ( !request.model.isEmpty() )
        body.set( "model", request.model );
    body.set( "n", 1 );
    body.set( "size", TQString( "%1x%2" ).arg( s.imageWidth ).arg( s.imageHeight ) );

    emit status( i18n( "Waiting for the image..." ) );
    HttpRequest *r = track( HttpRequest::postJson( address() + "/images/generations", body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageResult( HttpRequest * ) ) );
}

void OpenAIBackend::slotImageResult( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( r->errorString() );
        return;
    }
    JsonValue v = r->json();
    /* OpenAI: data[0].b64_json or data[0].url; Stable Diffusion web API: images[0] */
    const JsonValue &first = v.get( "data" ).at( 0 );
    TQString b64 = first.get( "b64_json" ).toString();
    if ( b64.isEmpty() )
        b64 = v.get( "images" ).at( 0 ).toString();
    const TQString url = first.get( "url" ).toString();

    if ( !b64.isEmpty() ) {
        const int comma = b64.startsWith( "data:" ) ? b64.find( ',' ) : -1;
        TQByteArray data;
        KCodecs::base64Decode( TQCString( b64.mid( comma + 1 ).latin1() ), data );
        if ( data.isEmpty() ) {
            fail( i18n( "The service sent an image that could not be decoded." ) );
            return;
        }
        const TQString revised = first.get( "revised_prompt" ).toString();
        if ( !revised.isEmpty() )
            emit textDelta( revised );
        emit media( saveMedia( data, TQString() ) );
        done();
    } else if ( !url.isEmpty() ) {
        emit status( i18n( "Fetching the image..." ) );
        HttpRequest *d = track( HttpRequest::get( url, TQStringList(), this ) );
        connect( d, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotImageDownloaded( HttpRequest * ) ) );
    } else {
        fail( i18n( "The service returned no image." ) );
    }
}

void OpenAIBackend::slotImageDownloaded( HttpRequest *r )
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

/* --------------------------------------------------------------- speech */

void OpenAIBackend::runSpeech( const Request &request )
{
    const Settings &s = Settings::self();
    JsonValue body = JsonValue::object();
    body.set( "model", request.model.isEmpty() ? s.openaiSpeechModel : request.model );
    body.set( "input", request.prompt );
    body.set( "voice", s.openaiVoice );
    body.set( "response_format", "mp3" );

    emit status( i18n( "Waiting for the audio..." ) );
    HttpRequest *r = track( HttpRequest::postJson( address() + "/audio/speech", body, headers(), this ) );
    connect( r, TQ_SIGNAL( finished( HttpRequest * ) ), this, TQ_SLOT( slotSpeechResult( HttpRequest * ) ) );
}

void OpenAIBackend::slotSpeechResult( HttpRequest *r )
{
    if ( !busy() )
        return;
    if ( !r->ok() ) {
        fail( r->errorString() );
        return;
    }
    Attachment a = saveMedia( r->body(), TQString() );
    if ( r->body().isEmpty() || a.kind != Attachment::Audio ) {
        TQFile::remove( a.path );
        fail( i18n( "The service did not return audio data: %1" ).arg( r->errorString() ) );
        return;
    }
    emit media( a );
    done();
}

#include "openaibackend.moc"
