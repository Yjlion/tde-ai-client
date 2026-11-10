/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "settingsdialog.h"
#include "settings.h"

#include <tqcheckbox.h>
#include <tqgroupbox.h>
#include <tqlabel.h>
#include <tqlayout.h>
#include <tqwhatsthis.h>

#include <kcombobox.h>
#include <kiconloader.h>
#include <klineedit.h>
#include <tdelocale.h>
#include <tdemessagebox.h>
#include <knuminput.h>
#include <ktextedit.h>

static TQLabel *wrapLabel( const TQString &text, TQWidget *parent )
{
    TQLabel *l = new TQLabel( text, parent );
    l->setAlignment( TQt::AlignLeft | TQt::AlignTop | TQt::WordBreak );
    return l;
}

static TQLabel *buddyLabel( const TQString &text, TQWidget *parent, TQWidget *buddy )
{
    TQLabel *l = new TQLabel( text, parent );
    l->setBuddy( buddy );
    return l;
}

KLineEdit *SettingsDialog::keyEdit( TQWidget *parent )
{
    KLineEdit *e = new KLineEdit( parent );
    e->setEchoMode( TQLineEdit::Password );
    return e;
}

SettingsDialog::SettingsDialog( TQWidget *parent, const char *name )
    : KDialogBase( IconList, i18n( "Configure" ),
                   Ok | Apply | Cancel | Default | Help, Ok, parent, name, true, true )
{
    setHelp( "configuration", "tde-ai-client" );

    /* ---- General ---- */
    TQFrame *gen = addPage( i18n( "General" ), i18n( "General Settings" ), BarIcon( "configure", TDEIcon::SizeMedium ) );
    TQVBoxLayout *gl = new TQVBoxLayout( gen, 0, spacingHint() );

    m_sendOnEnter = new TQCheckBox( i18n( "&Enter sends the message (Shift+Enter starts a new line)" ), gen );
    TQWhatsThis::add( m_sendOnEnter, i18n( "When this is off, Enter starts a new line and Ctrl+Enter sends the message." ) );
    gl->addWidget( m_sendOnEnter );

    m_useWallet = new TQCheckBox( i18n( "Keep API &keys in the TDE wallet" ), gen );
    TQWhatsThis::add( m_useWallet, i18n( "API keys are stored encrypted in TDEWallet instead of in the "
                                         "configuration file. You may be asked for the wallet password "
                                         "when the program starts." ) );
    gl->addWidget( m_useWallet );

    m_showKeys = new TQCheckBox( i18n( "S&how API keys while this dialog is open" ), gen );
    connect( m_showKeys, TQ_SIGNAL( toggled( bool ) ), this, TQ_SLOT( slotShowKeys( bool ) ) );
    gl->addWidget( m_showKeys );

    TQLabel *sl = new TQLabel( i18n( "S&ystem prompt (instructions given to the AI before every conversation):" ), gen );
    gl->addWidget( sl );
    m_systemPrompt = new KTextEdit( gen );
    m_systemPrompt->setTextFormat( TQt::PlainText );
    m_systemPrompt->setCheckSpellingEnabled( false );
    sl->setBuddy( m_systemPrompt );
    TQWhatsThis::add( m_systemPrompt, i18n( "For example: \"You are a helpful assistant. Answer briefly.\" "
                                            "Leave empty to send no instructions." ) );
    gl->addWidget( m_systemPrompt, 1 );

    /* ---- AI Horde ---- */
    TQFrame *horde = addPage( i18n( "AI Horde" ), i18n( "AI Horde - Volunteer Run Models" ),
                              BarIcon( "network", TDEIcon::SizeMedium ) );
    TQGridLayout *hl = new TQGridLayout( horde, 9, 2, 0, spacingHint() );
    hl->addMultiCellWidget( wrapLabel( i18n(
        "The <b>AI Horde</b> is a free service: volunteers share their computers to run text and image "
        "models. It works without an account. Registering at aihorde.net gives you an API key and "
        "\"kudos\", which move your requests forward in the queue." ), horde ), 0, 0, 0, 1 );
    m_hordeUrl = new KLineEdit( horde );
    hl->addWidget( buddyLabel( i18n( "&Address:" ), horde, m_hordeUrl ), 1, 0 );
    hl->addWidget( m_hordeUrl, 1, 1 );
    m_hordeKey = keyEdit( horde );
    TQWhatsThis::add( m_hordeKey, i18n( "Your AI Horde API key. 0000000000 (ten zeros) is the anonymous key." ) );
    hl->addWidget( buddyLabel( i18n( "API &key:" ), horde, m_hordeKey ), 2, 0 );
    hl->addWidget( m_hordeKey, 2, 1 );
    m_hordeFormat = new KComboBox( horde );
    m_hordeFormat->insertStringList( Settings::promptFormatNames() );
    TQWhatsThis::add( m_hordeFormat, i18n( "Text models on the Horde get the conversation as one piece of text. "
                                           "This is how the turns are marked in it; pick the format the "
                                           "chosen model was trained with. Alpaca works with most models." ) );
    hl->addWidget( buddyLabel( i18n( "&Prompt format:" ), horde, m_hordeFormat ), 3, 0 );
    hl->addWidget( m_hordeFormat, 3, 1 );
    m_hordeTrusted = new TQCheckBox( i18n( "Use &trusted workers only" ), horde );
    hl->addMultiCellWidget( m_hordeTrusted, 4, 4, 0, 1 );
    m_hordeSlow = new TQCheckBox( i18n( "Also use &slow workers" ), horde );
    hl->addMultiCellWidget( m_hordeSlow, 5, 5, 0, 1 );
    m_hordeNsfw = new TQCheckBox( i18n( "Allow images that are &not safe for work" ), horde );
    TQWhatsThis::add( m_hordeNsfw, i18n( "When this is off, workers are asked to censor such images." ) );
    hl->addMultiCellWidget( m_hordeNsfw, 6, 6, 0, 1 );
    hl->setRowStretch( 8, 1 );
    hl->setColStretch( 1, 1 );

    /* ---- KoboldCpp ---- */
    TQFrame *kobold = addPage( i18n( "KoboldCpp" ), i18n( "KoboldCpp - Local or LAN Server" ),
                               BarIcon( "computer", TDEIcon::SizeMedium ) );
    TQGridLayout *kl = new TQGridLayout( kobold, 6, 2, 0, spacingHint() );
    kl->addMultiCellWidget( wrapLabel( i18n(
        "<b>KoboldCpp</b> runs AI models on your own hardware. Enter the address of a KoboldCpp "
        "server on this computer or in your network, for example http://192.168.1.20:5001. "
        "What the server can do (vision, speech, images) is detected automatically." ), kobold ), 0, 0, 0, 1 );
    m_koboldUrl = new KLineEdit( kobold );
    kl->addWidget( buddyLabel( i18n( "&Address:" ), kobold, m_koboldUrl ), 1, 0 );
    kl->addWidget( m_koboldUrl, 1, 1 );
    m_koboldKey = keyEdit( kobold );
    TQWhatsThis::add( m_koboldKey, i18n( "Only needed if KoboldCpp was started with --password." ) );
    kl->addWidget( buddyLabel( i18n( "&Password:" ), kobold, m_koboldKey ), 2, 0 );
    kl->addWidget( m_koboldKey, 2, 1 );
    m_koboldVoice = new KLineEdit( kobold );
    TQWhatsThis::add( m_koboldVoice, i18n( "The voice for text to speech, for example kobo, cheery, sleepy or shouty." ) );
    kl->addWidget( buddyLabel( i18n( "Speech &voice:" ), kobold, m_koboldVoice ), 3, 0 );
    kl->addWidget( m_koboldVoice, 3, 1 );
    kl->setRowStretch( 5, 1 );
    kl->setColStretch( 1, 1 );

    /* ---- OpenAI-compatible ---- */
    TQFrame *openai = addPage( i18n( "OpenAI API" ), i18n( "OpenAI-compatible Services" ),
                               BarIcon( "connect_established", TDEIcon::SizeMedium ) );
    TQGridLayout *ol = new TQGridLayout( openai, 9, 2, 0, spacingHint() );
    ol->addMultiCellWidget( wrapLabel( i18n(
        "Many cloud services and local servers speak the <b>OpenAI API</b>: OpenAI, OpenRouter, "
        "Mistral, Groq, Ollama, llama.cpp, LocalAI and others. Enter the base address of the API "
        "(it usually ends in /v1) and the key the service gave you." ), openai ), 0, 0, 0, 1 );
    m_openaiUrl = new KComboBox( true, openai );
    m_openaiUrl->insertItem( "https://api.openai.com/v1" );
    m_openaiUrl->insertItem( "https://openrouter.ai/api/v1" );
    m_openaiUrl->insertItem( "https://api.mistral.ai/v1" );
    m_openaiUrl->insertItem( "https://api.groq.com/openai/v1" );
    m_openaiUrl->insertItem( "http://localhost:11434/v1" );
    m_openaiUrl->insertItem( "http://localhost:8080/v1" );
    ol->addWidget( buddyLabel( i18n( "&Address:" ), openai, m_openaiUrl ), 1, 0 );
    ol->addWidget( m_openaiUrl, 1, 1 );
    m_openaiKey = keyEdit( openai );
    ol->addWidget( buddyLabel( i18n( "API &key:" ), openai, m_openaiKey ), 2, 0 );
    ol->addWidget( m_openaiKey, 2, 1 );
    m_openaiSpeechModel = new KLineEdit( openai );
    ol->addWidget( buddyLabel( i18n( "&Speech model:" ), openai, m_openaiSpeechModel ), 3, 0 );
    ol->addWidget( m_openaiSpeechModel, 3, 1 );
    m_openaiVoice = new KLineEdit( openai );
    ol->addWidget( buddyLabel( i18n( "Speech &voice:" ), openai, m_openaiVoice ), 4, 0 );
    ol->addWidget( m_openaiVoice, 4, 1 );
    m_openaiTranscribe = new TQCheckBox( i18n( "&Transcribe audio attachments before sending them" ), openai );
    TQWhatsThis::add( m_openaiTranscribe, i18n( "Audio files are normally sent to the chat model as they are, which "
                                                "only works with models that can listen. With this option they are "
                                                "first turned into text by the transcription model." ) );
    ol->addMultiCellWidget( m_openaiTranscribe, 5, 5, 0, 1 );
    m_openaiTranscribeModel = new KLineEdit( openai );
    ol->addWidget( buddyLabel( i18n( "Transcription &model:" ), openai, m_openaiTranscribeModel ), 6, 0 );
    ol->addWidget( m_openaiTranscribeModel, 6, 1 );
    connect( m_openaiTranscribe, TQ_SIGNAL( toggled( bool ) ), m_openaiTranscribeModel, TQ_SLOT( setEnabled( bool ) ) );
    ol->setRowStretch( 8, 1 );
    ol->setColStretch( 1, 1 );

    /* ---- Generation ---- */
    TQFrame *g = addPage( i18n( "Generation" ), i18n( "Text and Image Generation" ),
                          BarIcon( "wizard", TDEIcon::SizeMedium ) );
    TQVBoxLayout *vl = new TQVBoxLayout( g, 0, spacingHint() );

    /* group boxes with two columns: label, input, label, input, ... */
    TQGroupBox *tg = new TQGroupBox( 2, TQt::Horizontal, i18n( "Text" ), g );
    TQLabel *l = new TQLabel( i18n( "&Longest answer (tokens):" ), tg );
    m_maxTokens = new KIntNumInput( tg );
    m_maxTokens->setRange( 16, 32768, 16, false );
    l->setBuddy( m_maxTokens );
    l = new TQLabel( i18n( "&Context sent to AI Horde models (tokens):" ), tg );
    m_contextLength = new KIntNumInput( tg );
    m_contextLength->setRange( 512, 1048576, 512, false );
    l->setBuddy( m_contextLength );
    TQWhatsThis::add( m_contextLength, i18n( "How much of the conversation the AI Horde may send to the model." ) );
    l = new TQLabel( i18n( "&Temperature:" ), tg );
    m_temperature = new KDoubleNumInput( 0.0, 2.0, 0.7, 0.05, 2, tg );
    l->setBuddy( m_temperature );
    TQWhatsThis::add( m_temperature, i18n( "Low values give predictable answers, high values more varied ones." ) );
    l = new TQLabel( i18n( "Top-&P:" ), tg );
    m_topP = new KDoubleNumInput( 0.0, 1.0, 0.9, 0.05, 2, tg );
    l->setBuddy( m_topP );
    vl->addWidget( tg );

    TQGroupBox *ig = new TQGroupBox( 2, TQt::Horizontal, i18n( "Images" ), g );
    l = new TQLabel( i18n( "&Width (pixels):" ), ig );
    m_imageWidth = new KIntNumInput( ig );
    m_imageWidth->setRange( 64, 4096, 64, false );
    l->setBuddy( m_imageWidth );
    l = new TQLabel( i18n( "&Height (pixels):" ), ig );
    m_imageHeight = new KIntNumInput( ig );
    m_imageHeight->setRange( 64, 4096, 64, false );
    l->setBuddy( m_imageHeight );
    l = new TQLabel( i18n( "&Steps:" ), ig );
    m_imageSteps = new KIntNumInput( ig );
    m_imageSteps->setRange( 1, 150, 1, false );
    l->setBuddy( m_imageSteps );
    l = new TQLabel( i18n( "&Guidance (CFG scale):" ), ig );
    m_imageCfg = new KDoubleNumInput( 1.0, 30.0, 7.0, 0.5, 1, ig );
    l->setBuddy( m_imageCfg );
    l = new TQLabel( i18n( "Change of an attached &image:" ), ig );
    m_imageDenoise = new KDoubleNumInput( 0.0, 1.0, 0.6, 0.05, 2, ig );
    l->setBuddy( m_imageDenoise );
    TQWhatsThis::add( m_imageDenoise, i18n( "When an image is attached as the starting point: how far the "
                                            "result may move away from it. 0 keeps it, 1 ignores it." ) );
    l = new TQLabel( i18n( "&Negative prompt:" ), ig );
    m_negativePrompt = new KLineEdit( ig );
    l->setBuddy( m_negativePrompt );
    TQWhatsThis::add( m_negativePrompt, i18n( "What should not be in generated images, e.g. \"blurry, text, watermark\"." ) );
    vl->addWidget( ig );
    vl->addStretch( 1 );

    load();
    resize( 640, 460 );
}

void SettingsDialog::slotShowKeys( bool show )
{
    const TQLineEdit::EchoMode mode = show ? TQLineEdit::Normal : TQLineEdit::Password;
    m_hordeKey->setEchoMode( mode );
    m_koboldKey->setEchoMode( mode );
    m_openaiKey->setEchoMode( mode );
}

void SettingsDialog::load()
{
    const Settings &s = Settings::self();
    m_sendOnEnter->setChecked( s.sendOnEnter );
    m_useWallet->setChecked( s.useWallet );
    m_systemPrompt->setText( s.systemPrompt );

    m_hordeUrl->setText( s.hordeUrl );
    m_hordeKey->setText( s.hordeKey );
    m_hordeFormat->setCurrentItem( s.hordePromptFormat );
    m_hordeNsfw->setChecked( s.hordeNsfw );
    m_hordeTrusted->setChecked( s.hordeTrustedOnly );
    m_hordeSlow->setChecked( s.hordeSlowWorkers );

    m_koboldUrl->setText( s.koboldUrl );
    m_koboldKey->setText( s.koboldKey );
    m_koboldVoice->setText( s.koboldVoice );

    m_openaiUrl->setEditText( s.openaiUrl );
    m_openaiKey->setText( s.openaiKey );
    m_openaiTranscribe->setChecked( s.openaiTranscribeFirst );
    m_openaiTranscribeModel->setText( s.openaiTranscribeModel );
    m_openaiTranscribeModel->setEnabled( s.openaiTranscribeFirst );
    m_openaiSpeechModel->setText( s.openaiSpeechModel );
    m_openaiVoice->setText( s.openaiVoice );

    m_maxTokens->setValue( s.maxTokens );
    m_contextLength->setValue( s.contextLength );
    m_temperature->setValue( s.temperature );
    m_topP->setValue( s.topP );
    m_imageWidth->setValue( s.imageWidth );
    m_imageHeight->setValue( s.imageHeight );
    m_imageSteps->setValue( s.imageSteps );
    m_imageCfg->setValue( s.imageCfgScale );
    m_imageDenoise->setValue( s.imageDenoise );
    m_negativePrompt->setText( s.negativePrompt );
}

void SettingsDialog::store()
{
    Settings &s = Settings::self();
    s.sendOnEnter = m_sendOnEnter->isChecked();
    s.useWallet = m_useWallet->isChecked();
    s.systemPrompt = m_systemPrompt->text();

    s.hordeUrl = m_hordeUrl->text().stripWhiteSpace();
    s.hordeKey = m_hordeKey->text().stripWhiteSpace();
    s.hordePromptFormat = m_hordeFormat->currentItem();
    s.hordeNsfw = m_hordeNsfw->isChecked();
    s.hordeTrustedOnly = m_hordeTrusted->isChecked();
    s.hordeSlowWorkers = m_hordeSlow->isChecked();

    s.koboldUrl = m_koboldUrl->text().stripWhiteSpace();
    s.koboldKey = m_koboldKey->text().stripWhiteSpace();
    s.koboldVoice = m_koboldVoice->text().stripWhiteSpace();

    s.openaiUrl = m_openaiUrl->currentText().stripWhiteSpace();
    s.openaiKey = m_openaiKey->text().stripWhiteSpace();
    s.openaiTranscribeFirst = m_openaiTranscribe->isChecked();
    s.openaiTranscribeModel = m_openaiTranscribeModel->text().stripWhiteSpace();
    s.openaiSpeechModel = m_openaiSpeechModel->text().stripWhiteSpace();
    s.openaiVoice = m_openaiVoice->text().stripWhiteSpace();

    s.maxTokens = m_maxTokens->value();
    s.contextLength = m_contextLength->value();
    s.temperature = m_temperature->value();
    s.topP = m_topP->value();
    s.imageWidth = m_imageWidth->value();
    s.imageHeight = m_imageHeight->value();
    s.imageSteps = m_imageSteps->value();
    s.imageCfgScale = m_imageCfg->value();
    s.imageDenoise = m_imageDenoise->value();
    s.negativePrompt = m_negativePrompt->text().stripWhiteSpace();

    if ( !s.save() ) {
        m_useWallet->setChecked( false );
        KMessageBox::sorry( this, i18n( "The TDE wallet could not be opened, so the API keys are kept in the "
                                        "configuration file as before." ) );
    }
    emit settingsChanged();
}

void SettingsDialog::slotOk()
{
    store();
    accept();
}

void SettingsDialog::slotApply()
{
    store();
}

void SettingsDialog::slotDefault()
{
    Settings &s = Settings::self();
    const Settings saved = s;
    s.setDefaults();
    s.lastBackend = saved.lastBackend;
    s.lastMode = saved.lastMode;
    load();
    s = saved;
}

#include "settingsdialog.moc"
