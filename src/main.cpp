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

#include "mainwindow.h"

#include <stdio.h>

#include <tqdir.h>
#include <tqfileinfo.h>

#include <tdeaboutdata.h>
#include <tdeapplication.h>
#include <tdecmdlineargs.h>
#include <kimageio.h>
#include <tdelocale.h>

static const char description[] =
    I18N_NOOP( "Chat with AI services from the Trinity Desktop: AI Horde, KoboldCpp "
               "and OpenAI-compatible servers" );

static TDECmdLineOptions options[] =
{
    { "b", 0, 0 },
    { "backend <service>", I18N_NOOP( "Service to use: horde, koboldcpp or openai" ), 0 },
    { "m", 0, 0 },
    { "mode <mode>", I18N_NOOP( "What to do: chat, image or speech" ), 0 },
    { "p", 0, 0 },
    { "prompt <text>", I18N_NOOP( "Send <text> (with the given files attached) on start-up" ), 0 },
    { "+[file]", I18N_NOOP( "Files to attach to the first message" ), 0 },
    TDECmdLineLastOption
};

int main( int argc, char **argv )
{
    TDEAboutData about( "tde-ai-client", I18N_NOOP( "TDE AI Client" ), VERSION, description,
                        TDEAboutData::License_GPL_V2,
                        I18N_NOOP( "(C) 2026 tde-ai-client contributors" ),
                        I18N_NOOP( "This program was largely written with the help of an AI coding "
                                   "assistant (\"vibe coded\"). It is provided as is, without any warranty.\n\n"
                                   "tde-ai-client is not affiliated with the AI Horde, KoboldCpp, OpenAI or "
                                   "any other service it can connect to. What you type and attach is sent "
                                   "to the service you select." ),
                        "https://github.com/Yjlion/tde-ai-client",
                        "https://github.com/Yjlion/tde-ai-client/issues" );
    about.addAuthor( "tde-ai-client contributors", I18N_NOOP( "Developers" ), 0,
                     "https://github.com/Yjlion/tde-ai-client" );
    about.addCredit( "AI Horde", I18N_NOOP( "Crowdsourced text and image generation" ), 0, "https://aihorde.net" );
    about.addCredit( "KoboldCpp", I18N_NOOP( "Local AI server" ), 0, "https://github.com/LostRuins/koboldcpp" );

    TDECmdLineArgs::init( argc, argv, &about );
    TDECmdLineArgs::addCmdLineOptions( options );
    TDEApplication app;
    KImageIO::registerFormats();

    MainWindow *win = 0;
    if ( app.isRestored() ) {
        RESTORE( MainWindow );
    } else {
        win = new MainWindow;
        app.setMainWidget( win );
        win->show();

        TDECmdLineArgs *args = TDECmdLineArgs::parsedArgs();
        if ( args->isSet( "backend" )
             && !win->selectBackend( TQString::fromLocal8Bit( args->getOption( "backend" ) ) ) )
            fprintf( stderr, "%s\n", i18n( "Unknown service \"%1\". Known are: horde, koboldcpp, openai." )
                     .arg( TQString::fromLocal8Bit( args->getOption( "backend" ) ) ).local8Bit().data() );
        TQStringList files;
        for ( int i = 0; i < args->count(); ++i )
            files << TQFileInfo( TQFile::decodeName( args->arg( i ) ) ).absFilePath();
        win->startWith( args->isSet( "mode" ) ? TQString::fromLocal8Bit( args->getOption( "mode" ) ) : TQString(),
                        files,
                        args->isSet( "prompt" ) ? TQString::fromLocal8Bit( args->getOption( "prompt" ) ) : TQString() );
        args->clear();
    }

    return app.exec();
}
