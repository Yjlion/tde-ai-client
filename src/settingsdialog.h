/*
 * tde-ai-client - an AI client for the Trinity Desktop Environment
 * Copyright (C) 2026 tde-ai-client contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef TDEAICLIENT_SETTINGSDIALOG_H
#define TDEAICLIENT_SETTINGSDIALOG_H

#include <kdialogbase.h>

class TQCheckBox;
class KComboBox;
class KDoubleNumInput;
class KIntNumInput;
class KLineEdit;
class KPasswordEdit;
class KTextEdit;

class SettingsDialog : public KDialogBase
{
    TQ_OBJECT

public:
    enum Page { PageGeneral = 0, PageHorde, PageKobold, PageOpenAI, PageGeneration };

    SettingsDialog( TQWidget *parent, const char *name = 0 );

signals:
    void settingsChanged();

protected slots:
    void slotOk();
    void slotApply();
    void slotDefault();

private slots:
    void slotShowKeys( bool show );

private:
    void load();
    void store();
    KLineEdit *keyEdit( TQWidget *parent );

    /* general */
    TQCheckBox      *m_sendOnEnter;
    TQCheckBox      *m_useWallet;
    TQCheckBox      *m_showKeys;
    KTextEdit       *m_systemPrompt;
    /* AI Horde */
    KLineEdit       *m_hordeUrl;
    KLineEdit       *m_hordeKey;
    KComboBox       *m_hordeFormat;
    TQCheckBox      *m_hordeNsfw;
    TQCheckBox      *m_hordeTrusted;
    TQCheckBox      *m_hordeSlow;
    /* KoboldCpp */
    KLineEdit       *m_koboldUrl;
    KLineEdit       *m_koboldKey;
    KLineEdit       *m_koboldVoice;
    /* OpenAI-compatible */
    KComboBox       *m_openaiUrl;
    KLineEdit       *m_openaiKey;
    TQCheckBox      *m_openaiTranscribe;
    KLineEdit       *m_openaiTranscribeModel;
    KLineEdit       *m_openaiSpeechModel;
    KLineEdit       *m_openaiVoice;
    /* generation */
    KIntNumInput    *m_maxTokens;
    KIntNumInput    *m_contextLength;
    KDoubleNumInput *m_temperature;
    KDoubleNumInput *m_topP;
    KIntNumInput    *m_imageWidth;
    KIntNumInput    *m_imageHeight;
    KIntNumInput    *m_imageSteps;
    KDoubleNumInput *m_imageCfg;
    KDoubleNumInput *m_imageDenoise;
    KLineEdit       *m_negativePrompt;
};

#endif
