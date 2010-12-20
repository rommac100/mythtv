
// -*- Mode: c++ -*-

// Standard UNIX C headers
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

// Qt headers
#include <QCoreApplication>
#include <QEvent>
#include <QDir>

// MythTV headers
#include "mythconfig.h"
#include "mythcorecontext.h"
#include "mythdbcon.h"
#include "mythverbose.h"
#include "audiooutpututil.h"
#include "audiogeneralsettings.h"
#include "mythdialogbox.h"

extern "C" {
#include "libavformat/avformat.h"
}


class TriggeredItem : public TriggeredConfigurationGroup
{
  public:
    TriggeredItem(Setting *checkbox, ConfigurationGroup *group) :
        TriggeredConfigurationGroup(false, false, false, false)
    {
        setTrigger(checkbox);

        addTarget("1", group);
        addTarget("0", new VerticalConfigurationGroup(true));
    }
    TriggeredItem(Setting *checkbox, Setting *setting) :
        TriggeredConfigurationGroup(false, false, false, false)
    {
        setTrigger(checkbox);

        addTarget("1", setting);
        addTarget("0", new VerticalConfigurationGroup(false, false));
    }
};

AudioDeviceComboBox::AudioDeviceComboBox(AudioConfigSettings *parent) :
    HostComboBox("AudioOutputDevice", true), m_parent(parent)
{
    setLabel(QObject::tr("Audio output device"));
#ifdef USING_ALSA
    QString dflt = "ALSA:default";
#elif USING_PULSEOUTPUT
    QString dflt = "PulseAudio:default";
#elif CONFIG_DARWIN
    QString dflt = "CoreAudio:";
#elif USING_MINGW
    QString dflt = "Windows:";
#else
    QString dflt = "NULL";
#endif
    QString current = gCoreContext->GetSetting(QString("AudioOutputDevice"),
                                               dflt);
    addSelection(current, current, true);

    connect(this, SIGNAL(valueChanged(const QString&)),
            this, SLOT(AudioDescriptionHelp(const QString&)));
}

void AudioDeviceComboBox::AudioRescan()
{
    AudioOutput::ADCVect &vect = m_parent->AudioDeviceVect();
    AudioOutput::ADCVect::const_iterator it;

    if (vect.empty())
        return;

    QString value = getValue();
    clearSelections();
    resetMaxCount(vect.size());

    bool found = false;
    for (it = vect.begin(); it != vect.end(); it++)
        addSelection(it->name, it->name,
                     value == it->name ? (found = true) : false);
    if (!found)
    {
        resetMaxCount(vect.size()+1);
        addSelection(value, value, true);
    }
        // For some reason, it adds an empty entry, remove it
    removeSelection(QString::null);
}

void AudioDeviceComboBox::AudioDescriptionHelp(const QString &device)
{
    QString desc = m_parent->AudioDeviceMap().value(device).desc;
    setHelpText(desc);
}

AudioConfigSettings::AudioConfigSettings(ConfigurationWizard *parent) :
    VerticalConfigurationGroup(false, true, false, false),
    m_OutputDevice(NULL),   m_MaxAudioChannels(NULL),
    m_AudioUpmix(NULL),     m_AudioUpmixType(NULL),
    m_AC3PassThrough(NULL), m_DTSPassThrough(NULL),
    m_EAC3PassThrough(NULL),m_TrueHDPassThrough(NULL),
    m_passthrough8(false),  m_parent(parent)
{
    setLabel(QObject::tr("Audio System"));
    setUseLabel(false);

    ConfigurationGroup *devicegroup = new HorizontalConfigurationGroup(false,
                                                                       false);
    devicegroup->addChild((m_OutputDevice = new AudioDeviceComboBox(this)));
        // Rescan button
    TransButtonSetting *rescan = new TransButtonSetting("rescan");
    rescan->setLabel(QObject::tr("Rescan"));
    rescan->setHelpText(QObject::tr("Rescan for available audio devices. "
                                    "Current entry will be checked and "
                                    "capability entries populated."));
    devicegroup->addChild(rescan);
    connect(rescan, SIGNAL(pressed()), this, SLOT(AudioRescan()));
    addChild(devicegroup);

    QString name = m_OutputDevice->getValue();
    AudioOutput::AudioDeviceConfig *adc =
        AudioOutput::GetAudioDeviceConfig(name, name, true);
    if (adc->settings.IsInvalid())
    {
        VERBOSE(VB_IMPORTANT, QString("Audio device %1 isn't usable "
                                      "Check audio configuration").arg(name));
    }
    audiodevs.insert(name, *adc);
    devices.append(*adc);

    delete adc;
    CheckPassthrough();

    ConfigurationGroup *maingroup = new VerticalConfigurationGroup(false,
                                                                   false);
    addChild(maingroup);

    m_triggerDigital = new TransCheckBoxSetting();
    m_AC3PassThrough = AC3PassThrough();
    m_DTSPassThrough = DTSPassThrough();
    m_EAC3PassThrough = EAC3PassThrough();
    m_TrueHDPassThrough = TrueHDPassThrough();

    m_cgsettings = new HorizontalConfigurationGroup();
    m_cgsettings->setLabel(QObject::tr("Digital Audio Capabilities"));
    m_cgsettings->addChild(m_AC3PassThrough);
    m_cgsettings->addChild(m_DTSPassThrough);
    m_cgsettings->addChild(m_EAC3PassThrough);
    m_cgsettings->addChild(m_TrueHDPassThrough);

    TriggeredItem *sub1 = new TriggeredItem(m_triggerDigital, m_cgsettings);

    maingroup->addChild(sub1);

    maingroup->addChild((m_MaxAudioChannels = MaxAudioChannels()));
    maingroup->addChild((m_AudioUpmix = AudioUpmix()));
    maingroup->addChild((m_AudioUpmixType = AudioUpmixType()));

    TransButtonSetting *test = new TransButtonSetting("test");
    test->setLabel(QObject::tr("Test"));
    test->setHelpText(QObject::tr("Will play a test pattern on all configured "
                                  "speakers"));
    connect(test, SIGNAL(pressed()), this, SLOT(StartAudioTest()));
    addChild(test);

    TransButtonSetting *advanced = new TransButtonSetting("advanced");
    advanced->setLabel(QObject::tr("Advanced Audio Settings"));
    advanced->setHelpText(QObject::tr("Enable extra audio settings. Under most "
                                  "usage all options should be unchecked"));
    connect(advanced, SIGNAL(pressed()), this, SLOT(AudioAdvanced()));
    addChild(advanced);

        // Set slots
    connect(m_MaxAudioChannels, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateVisibility(const QString&)));
    connect(m_OutputDevice, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateCapabilities(const QString&)));
    connect(m_AC3PassThrough, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateCapabilities(const QString&)));
    connect(m_DTSPassThrough, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateCapabilities(const QString&)));
    connect(m_EAC3PassThrough, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateCapabilities(const QString&)));
    connect(m_TrueHDPassThrough, SIGNAL(valueChanged(const QString&)),
            this, SLOT(UpdateCapabilities(const QString&)));
    AudioRescan();
}

void AudioConfigSettings::AudioRescan()
{
    if (!slotlock.tryLock())
        return;

    QVector<AudioOutput::AudioDeviceConfig>* list =
        AudioOutput::GetOutputList();
    QVector<AudioOutput::AudioDeviceConfig>::const_iterator it;

    audiodevs.clear();
    for (it = list->begin(); it != list->end(); it++)
        audiodevs.insert(it->name, *it);

    devices = *list;
    delete list;

    QString name = m_OutputDevice->getValue();
    if (!audiodevs.contains(name))
    {
            // Scan for possible custom entry that isn't in the list
        AudioOutput::AudioDeviceConfig *adc =
            AudioOutput::GetAudioDeviceConfig(name, name, true);
        if (adc->settings.IsInvalid())
        {
            QString msg = name + QObject::tr(" is invalid or not useable.");
            MythPopupBox::showOkPopup(
                GetMythMainWindow(), QObject::tr("Warning"), msg);
            VERBOSE(VB_IMPORTANT, QString("Audio device %1 isn't usable ")
                    .arg(name));
        }
        audiodevs.insert(name, *adc);
        devices.append(*adc);
        delete adc;
    }
    m_OutputDevice->AudioRescan();
    slotlock.unlock();
    UpdateCapabilities(QString::null);
}

void AudioConfigSettings::UpdateVisibility(const QString &device)
{
    if (!m_MaxAudioChannels && !m_AudioUpmix && !m_AudioUpmixType)
        return;

    int cur_speakers = m_MaxAudioChannels->getValue().toInt();
    m_AudioUpmix->setEnabled(cur_speakers > 2);
    m_AudioUpmixType->setEnabled(cur_speakers > 2);
}

AudioOutputSettings AudioConfigSettings::UpdateCapabilities(
    const QString &device)
{
    int max_speakers = 8;
    int realmax_speakers = 8;

    bool invalid = false;
    AudioOutputSettings settings;

        // Test if everything is set yet
    if (!m_OutputDevice    || !m_MaxAudioChannels   ||
        !m_AC3PassThrough  || !m_DTSPassThrough     ||
        !m_EAC3PassThrough || !m_TrueHDPassThrough)
        return settings;

    if (!slotlock.tryLock()) // Doing a rescan of channels
        return settings;

    bool bForceDigital = gCoreContext->GetNumSetting("PassThruDeviceOverride",
                                                     false);
    bool bAC3 = true;
    bool bDTS = true;
    bool bLPCM = true;
    bool bHD = true;
    bool bHDLL = true;

    QString out = m_OutputDevice->getValue();
    if (!audiodevs.contains(out))
    {
        VERBOSE(VB_AUDIO, QString("Update not found (%1)").arg(out));
        invalid = true;
    }
    else
    {
        settings = audiodevs.value(out).settings;

        realmax_speakers = max_speakers = settings.BestSupportedChannels();

        bAC3  = (settings.canAC3() || bForceDigital) &&
            m_AC3PassThrough->boolValue();
        bDTS  = (settings.canDTS() || bForceDigital) &&
            m_DTSPassThrough->boolValue();
        bLPCM = settings.canLPCM() &&
            !gCoreContext->GetNumSetting("StereoPCM", false);
        bHD = ((bLPCM && settings.canHD()) || bForceDigital) &&
            m_EAC3PassThrough->boolValue() &&
            !gCoreContext->GetNumSetting("Audio48kOverride", false);
        bHDLL = ((bLPCM && settings.canHDLL()) || bForceDigital) &&
            m_TrueHDPassThrough->boolValue() &&
            !gCoreContext->GetNumSetting("Audio48kOverride", false);

        if (max_speakers > 2 && !bLPCM)
            max_speakers = 2;
        if (max_speakers == 2 && (bAC3 || bDTS))
            max_speakers = 6;
    }

    m_triggerDigital->setValue(invalid || bForceDigital ||
                               settings.canAC3() || settings.canDTS());
    m_EAC3PassThrough->setEnabled(settings.canHD() && bLPCM);
    m_TrueHDPassThrough->setEnabled(settings.canHDLL() & bLPCM);

    int cur_speakers = m_MaxAudioChannels->getValue().toInt();

    if (cur_speakers > max_speakers)
    {
        VERBOSE(VB_AUDIO, QString("Reset device %1").arg(out));
        cur_speakers = max_speakers;
    }

        // Remove everything and re-add available channels
    m_MaxAudioChannels->clearSelections();
    m_MaxAudioChannels->resetMaxCount(3);
    for (int i = 1; i <= max_speakers; i++)
    {
        if (invalid || settings.IsSupportedChannels(i) ||
            (bForceDigital && i >= 6))
        {
            QString txt;

            switch (i)
            {
                case 2:
                    txt = QObject::tr("Stereo");
                    break;
                case 6:
                    txt = QObject::tr("5.1");
                    break;
                case 8:
                    txt = QObject::tr("7.1");
                    break;
                default:
                    continue;
            }
            m_MaxAudioChannels->addSelection(txt, QString::number(i),
                                             i == cur_speakers);
        }
    }
    settings.SetBestSupportedChannels(cur_speakers);
    settings.setAC3(bAC3);
    settings.setDTS(bDTS);
    settings.setLPCM(bLPCM && (realmax_speakers > 2));

    slotlock.unlock();
    return settings;
}

void AudioConfigSettings::AudioAdvanced()
{
    QString out  = m_OutputDevice->getValue();
    bool invalid = false;
    AudioOutputSettings settings;

    if (!audiodevs.contains(out))
    {
        invalid = true;
    }
    else
    {
        settings = audiodevs.value(out).settings;
    }

    AudioAdvancedSettingsGroup audiosettings(invalid ||
                                             (settings.canLPCM() &&
                                              settings.canPassthrough() >= 0));

    if (audiosettings.exec() == kDialogCodeAccepted)
    {
        CheckPassthrough();
        UpdateCapabilities(QString::null);
    }
}

HostComboBox *AudioConfigSettings::MaxAudioChannels()
{
    QString name = "MaxChannels";
    HostComboBox *gc = new HostComboBox(name, false);
    gc->setLabel(QObject::tr("Speaker configuration"));
    gc->addSelection(QObject::tr("Stereo"), "2", true); // default
    gc->setHelpText(QObject::tr("Select the maximum number of audio "
                                "channels supported by your receiver "
                                "and speakers."));
    return gc;
}

HostCheckBox *AudioConfigSettings::AudioUpmix()
{
    HostCheckBox *gc = new HostCheckBox("AudioDefaultUpmix");
    gc->setLabel(QObject::tr("Upconvert stereo to 5.1 surround"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, MythTV will upconvert stereo "
                    "to 5.1 audio. You can enable or disable "
                    "the upconversion during playback at any time."));
    return gc;
}

HostComboBox *AudioConfigSettings::AudioUpmixType()
{
    HostComboBox *gc = new HostComboBox("AudioUpmixType",false);
    gc->setLabel(QObject::tr("Upmix Quality"));
    gc->addSelection(QObject::tr("Good"), "1");
    gc->addSelection(QObject::tr("Best"), "2", true);  // default
    gc->setHelpText(QObject::tr("Set the audio surround-upconversion quality."));
    return gc;
}

HostCheckBox *AudioConfigSettings::AC3PassThrough()
{
    HostCheckBox *gc = new HostCheckBox("AC3PassThru");
    gc->setLabel(QObject::tr("Dolby Digital"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable if your amplifier or sound decoder "
                    "supports AC3/Dolby Digital. You must use a digital "
                    "connection. Uncheck if using an analog connection."));
    return gc;
}

HostCheckBox *AudioConfigSettings::DTSPassThrough()
{
    HostCheckBox *gc = new HostCheckBox("DTSPassThru");
    gc->setLabel(QObject::tr("DTS"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable if your amplifier or sound decoder "
                    "supports DTS. You must use a digital connection. Uncheck "
                    "if using an analog connection"));
    return gc;
}

HostCheckBox *AudioConfigSettings::EAC3PassThrough()
{
    HostCheckBox *gc = new HostCheckBox("EAC3PassThru");
    gc->setLabel(QObject::tr("E-AC3/DTS-HD"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable if your amplifier or sound decoder "
                    "supports E-AC3 (DD+) or DTS-HD. You must use a hdmi "
                    "connection."));
    return gc;
}

HostCheckBox *AudioConfigSettings::TrueHDPassThrough()
{
    HostCheckBox *gc = new HostCheckBox("TrueHDPassThru");
    gc->setLabel(QObject::tr("TrueHD/DTS-HD MA"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable if your amplifier or sound decoder "
                    "supports Dolby TrueHD. You must use a hdmi connection."));
    return gc;
}

bool AudioConfigSettings::CheckPassthrough()
{
    m_passthrough8 = false;
    if (gCoreContext->GetNumSetting("PassThruDeviceOverride", false))
    {
        QString name = gCoreContext->GetSetting("PassThruOutputDevice");
        AudioOutput::AudioDeviceConfig *adc =
            AudioOutput::GetAudioDeviceConfig(name, name, true);
        if (adc->settings.IsInvalid())
        {
            VERBOSE(VB_IMPORTANT, QString("Passthru device %1 isn't usable "
                                 "Check audio configuration").arg(name));
        }
        else
        {
            if (adc->settings.BestSupportedChannels() >= 8)
            {
                m_passthrough8 = true;
            }
        }
        delete adc;
    }
    return m_passthrough8;
}

void AudioConfigSettings::StartAudioTest()
{
    AudioOutputSettings settings = UpdateCapabilities(QString::null);
    
    QString out  = m_OutputDevice->getValue();
    QString passthrough =
        gCoreContext->GetNumSetting("PassThruDeviceOverride", false) ?
        gCoreContext->GetSetting("PassThruOutputDevice") : QString::null;
    int channels = m_MaxAudioChannels->getValue().toInt();
    QString errMsg;

    AudioTestGroup audiotest(out, passthrough, channels, settings);

    audiotest.exec();
}

AudioTestThread::AudioTestThread(QObject *parent,
                                 QString main, QString passthrough,
                                 int channels,
                                 AudioOutputSettings settings,
                                 bool hd) :
    m_parent(parent), m_channels(channels), m_device(main),
    m_passthrough(passthrough), m_interrupted(false), m_channel(-1), m_hd(hd)
{
    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();

    m_format = hd ? settings.BestSupportedFormat() : FORMAT_S16;
    m_samplerate = hd ? settings.BestSupportedRate() : 48000;

    m_audioOutput = AudioOutput::OpenAudio(m_device, m_passthrough,
                                           m_format, m_channels,
                                           0, m_samplerate,
                                           AUDIOOUTPUT_VIDEO,
                                           true, false, 0, &settings);
    if (result().isEmpty())
    {
        m_audioOutput->Pause(true);
    }
}

QEvent::Type ChannelChangedEvent::kEventType =
    (QEvent::Type) QEvent::registerEventType();

AudioTestThread::~AudioTestThread()
{
    cancel();
    wait();
    if (m_audioOutput)
        delete m_audioOutput;
}

void AudioTestThread::cancel()
{
    m_interrupted = true;
}

QString AudioTestThread::result()
{
    QString errMsg;
    if (!m_audioOutput)
        errMsg = QObject::tr("Unable to create AudioOutput.");
    else
        errMsg = m_audioOutput->GetError();
    return errMsg;
}

void AudioTestThread::setChannel(int channel)
{
    m_channel = channel;
}

void AudioTestThread::run()
{
    m_interrupted = false;
    int smptelayout[7][8] = { 
        { 0, 1 },                       //stereo
        { },                            //not used
        { },                            //not used
        { },                            //not used
        { 0, 2, 1, 5, 4, 3 },           //5.1
        { },                            //not used
        { 0, 2, 1, 7,  5, 4, 6, 3 },    //7.1
    };

    if (m_audioOutput)
    {
        char *frames_in = new char[m_channels * 1024 * sizeof(int32_t) + 15];
        char *frames = (char *)(((long)frames_in + 15) & ~0xf);

        m_audioOutput->Pause(false);

        int begin = 0;
        int end = m_channels;
        if (m_channel >= 0)
        {
            begin = m_channel;
            end = m_channel + 1;
        }
        while (!m_interrupted)
        {
            for (int i = begin; i < end && !m_interrupted; i++)
            {
                int current = smptelayout[m_channels - 2][i];

                if (m_parent)
                {
                    QString channel;

                    switch(current)
                    {
                        case 0:
                            channel = "frontleft";
                            break;
                        case 1:
                            channel = "frontright";
                            break;
                        case 2:
                            channel = "center";
                            break;
                        case 3:
                            channel = "lfe";
                            break;
                        case 4:
                            if (m_channels == 6)
                                channel = "surroundleft";
                            else
                                channel = "rearleft";
                            break;
                        case 5:
                            if (m_channels == 6)
                                channel = "surroundright";
                            else
                                channel = "rearright";
                            break;
                        case 6:
                            channel = "surroundleft";
                            break;
                        case 7:
                            channel = "surroundright";
                            break;
                    }
                    QCoreApplication::postEvent(
                        m_parent, new ChannelChangedEvent(channel,
                                                          m_channel < 0));
                    VERBOSE(VB_AUDIO, QString("AudioTest: %1 (%2->%3)")
                            .arg(channel).arg(i).arg(current));
                }

                    // play sample sound for about 3s
                int top = m_samplerate / 1000 * 3;
                for (int j = 0; j < top && !m_interrupted; j++)
                {
                    AudioOutputUtil::GeneratePinkSamples(frames, m_channels,
                                                         current, 1000,
                                                         m_hd ? 32 : 16);
                    if (!m_audioOutput->AddFrames(frames, 1000, -1))
                    {
                        VERBOSE(VB_AUDIO, "AddAudioData() "
                                "Audio buffer overflow, audio data lost!");
                    }
                     // a tad less than 1/48th of a second to avoid underruns
                    usleep((1000000 / m_samplerate) * 1000);
                }
                m_audioOutput->Drain();
                m_audioOutput->Pause(true);
                usleep(500000); // .5s pause
                m_audioOutput->Pause(false);
            }
            if (m_channel >= 0)
                break;
        }
        m_audioOutput->Pause(true);

        delete[] frames_in;
    }
}

AudioTest::AudioTest(QString main, QString passthrough,
                     int channels, AudioOutputSettings settings) 
    : VerticalConfigurationGroup(false, true, false, false),
      m_channels(channels),
      m_frontleft(NULL), m_frontright(NULL), m_center(NULL),
      m_surroundleft(NULL), m_surroundright(NULL),
      m_rearleft(NULL), m_rearright(NULL), m_lfe(NULL),
      m_main(main), m_passthrough(passthrough), m_settings(settings),
      m_quality(false)
{
    setLabel(QObject::tr("Audio Configuration Testing"));

    m_at = new AudioTestThread(this, main, passthrough, channels,
                               settings, m_quality);
    if (!m_at->result().isEmpty())
    {
        QString msg = main + QObject::tr(" is invalid or not "
                                         "useable.");
        MythPopupBox::showOkPopup(
            GetMythMainWindow(), QObject::tr("Warning"), msg);
        return;
    }

    m_button = new TransButtonSetting("start");
    m_button->setLabel(QObject::tr("Test All"));
    m_button->setHelpText(QObject::tr("Start all channels test"));
    connect(m_button, SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));

    ConfigurationGroup *frontgroup =
        new HorizontalConfigurationGroup(false,
                                         false);
    ConfigurationGroup *middlegroup =
        new HorizontalConfigurationGroup(false,
                                         false);
    ConfigurationGroup *reargroup =
        new HorizontalConfigurationGroup(false,
                                         false);
    m_frontleft = new TransButtonSetting("0");
    m_frontleft->setLabel(QObject::tr("Front Left"));
    connect(m_frontleft,
            SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));
    m_frontright = new TransButtonSetting("2");
    m_frontright->setLabel(QObject::tr("Front Right"));
    connect(m_frontright,
            SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));
    m_center = new TransButtonSetting("1");
    m_center->setLabel(QObject::tr("Center"));
    connect(m_center,
            SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));

    frontgroup->addChild(m_frontleft);

    switch(m_channels)
    {
        case 8:
            m_rearleft = new TransButtonSetting("5");
            m_rearleft->setLabel(QObject::tr("Rear Left"));
            connect(m_rearleft,
                    SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));
            m_rearright = new TransButtonSetting("4");
            m_rearright->setLabel(QObject::tr("Rear Right"));
            connect(m_rearright,
                    SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));

            reargroup->addChild(m_rearleft);
            reargroup->addChild(m_rearright);
    
        case 6:
            m_surroundleft = new TransButtonSetting(m_channels == 6 ?
                                                    "4" : "6");
            m_surroundleft->setLabel(QObject::tr("Surround Left"));
            connect(m_surroundleft,
                    SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));
            m_surroundright = new TransButtonSetting("3");
            m_surroundright->setLabel(QObject::tr("Surround Right"));
            connect(m_surroundright,
                    SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));
            m_lfe = new TransButtonSetting(m_channels == 6 ? "5" : "7");
            m_lfe->setLabel(QObject::tr("LFE"));
            connect(m_lfe,
                    SIGNAL(pressed(QString)), this, SLOT(toggle(QString)));

            frontgroup->addChild(m_center);
            middlegroup->addChild(m_surroundleft);
            middlegroup->addChild(m_lfe);
            middlegroup->addChild(m_surroundright);

        case 2:
            break;
    }
    frontgroup->addChild(m_frontright);
    addChild(frontgroup);
    addChild(middlegroup);
    addChild(reargroup);
    addChild(m_button);

    m_hd = new TransCheckBoxSetting();
    m_hd->setLabel(QObject::tr("Use Highest Quality Mode"));
    m_hd->setHelpText(QObject::tr("Use the highest audio quality settings "
                                  "supported by your audio card. This will be "
                                  "a good place to start troubleshooting "
                                  "potential errors"));
    addChild(m_hd);
    connect(m_hd, SIGNAL(valueChanged(QString)), this, SLOT(togglequality()));
}

AudioTest::~AudioTest()
{
    m_at->cancel();
    m_at->wait();
    delete m_at;
}

void AudioTest::toggle(QString str)
{
    if (str == "start")
    {
        if (m_at->isRunning())
        {
            m_at->cancel();
            m_button->setLabel(QObject::tr("Test All"));
            if (m_frontleft)
                m_frontleft->setEnabled(true);
            if (m_frontright)
                m_frontright->setEnabled(true);
            if (m_center)
                m_center->setEnabled(true);
            if (m_surroundleft)
                m_surroundleft->setEnabled(true);
            if (m_surroundright)
                m_surroundright->setEnabled(true);
            if (m_rearleft)
                m_rearleft->setEnabled(true);
            if (m_rearright)
                m_rearright->setEnabled(true);
            if (m_lfe)
                m_lfe->setEnabled(true);
        }
        else
        {
            m_at->setChannel(-1);
            m_at->start();
            m_button->setLabel(QObject::tr("Stop"));
        }
        return;
    }
    if (m_at->isRunning())
    {
        m_at->cancel();
        m_at->wait();
    }

    int channel = str.toInt();
    m_at->setChannel(channel);

    m_at->start();
}

void AudioTest::togglequality()
{
    if (m_at->isRunning())
    {
        toggle("start");
    }

    m_quality = m_hd->boolValue();
    delete m_at;
    m_at = new AudioTestThread(this, m_main, m_passthrough, m_channels,
                               m_settings, m_quality);
    if (!m_at->result().isEmpty())
    {
        QString msg = QObject::tr("Audio device is invalid or not useable.");
        MythPopupBox::showOkPopup(
            GetMythMainWindow(), QObject::tr("Warning"), msg);
    }
}

bool AudioTest::event(QEvent *event)
{
    if (event->type() != ChannelChangedEvent::kEventType)
        return QObject::event(event); //not handled

    ChannelChangedEvent *cce = (ChannelChangedEvent*)(event);
    QString channel          = cce->channel;

    if (!cce->fulltest)
        return false;

    bool fl, fr, c, lfe, sl, sr, rl, rr;
    fl = fr = c = lfe = sl = sr = rl = rr = false;

    if (channel == "frontleft")
    {
        fl = true;
    }
    else if (channel == "frontright")
    {
        fr = true;
    }
    else if (channel == "center")
    {
        c = true;
    }
    else if (channel == "lfe")
    {
        lfe = true;
    }
    else if (channel == "surroundleft")
    {
        sl = true;
    }
    else if (channel == "surroundright")
    {
        sr = true;
    }
    else if (channel == "rearleft")
    {
        rl = true;
    }
    else if (channel == "rearright")
    {
        rr = true;
    }
    if (m_frontleft)
        m_frontleft->setEnabled(fl);
    if (m_frontright)
        m_frontright->setEnabled(fr);
    if (m_center)
        m_center->setEnabled(c);
    if (m_surroundleft)
        m_surroundleft->setEnabled(sl);
    if (m_surroundright)
        m_surroundright->setEnabled(sr);
    if (m_rearleft)
        m_rearleft->setEnabled(rl);
    if (m_rearright)
        m_rearright->setEnabled(rr);
    if (m_lfe)
        m_lfe->setEnabled(lfe);
    return false;
}


AudioTestGroup::AudioTestGroup(QString main, QString passthrough,
                               int channels, AudioOutputSettings settings)
{
    addChild(new AudioTest(main, passthrough, channels, settings));
}

HostCheckBox *AudioMixerSettings::MythControlsVolume()
{
    HostCheckBox *gc = new HostCheckBox("MythControlsVolume");
    gc->setLabel(QObject::tr("Use internal volume controls"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, MythTV will control the PCM and "
                    "master mixer volume. Disable this option if you prefer "
                    "to control the volume externally (for example, using "
                    "your amplifier) or if you use an external mixer program."));
    return gc;
}

HostComboBox *AudioMixerSettings::MixerDevice()
{
    HostComboBox *gc = new HostComboBox("MixerDevice", true);
    gc->setLabel(QObject::tr("Mixer device"));

#ifdef USING_OSS
    QDir dev("/dev", "mixer*", QDir::Name, QDir::System);
    gc->fillSelectionsFromDir(dev);

    dev.setPath("/dev/sound");
    if (dev.exists())
    {
        gc->fillSelectionsFromDir(dev);
    }
#endif
#ifdef USING_ALSA
    gc->addSelection("ALSA:default", "ALSA:default");
#endif
#ifdef USING_MINGW
    gc->addSelection("DirectX:", "DirectX:");
    gc->addSelection("Windows:", "Windows:");
#endif
#if !defined(USING_MINGW)
    gc->addSelection("software", "software");
    gc->setHelpText(QObject::tr("Setting the mixer device to \"software\" "
                    "lets MythTV control the volume of all audio at the "
                    "expense of a slight quality loss."));
#endif

    return gc;
}

const char* AudioMixerSettings::MixerControlControls[] = { "PCM",
                                                           "Master" };

HostComboBox *AudioMixerSettings::MixerControl()
{
    HostComboBox *gc = new HostComboBox("MixerControl", true);
    gc->setLabel(QObject::tr("Mixer controls"));
    for (unsigned int i = 0; i < sizeof(MixerControlControls) / sizeof(char*);
         ++i)
    {
        gc->addSelection(QObject::tr(MixerControlControls[i]),
                         MixerControlControls[i]);
    }

    gc->setHelpText(QObject::tr("Changing the volume adjusts the selected mixer."));
    return gc;
}

HostSlider *AudioMixerSettings::MixerVolume()
{
    HostSlider *gs = new HostSlider("MasterMixerVolume", 0, 100, 1);
    gs->setLabel(QObject::tr("Master mixer volume"));
    gs->setValue(70);
    gs->setHelpText(QObject::tr("Initial volume for the Master mixer. "
                    "This affects all sound created by the audio device. "
                    "Note: Do not set this too low."));
    return gs;
}

HostSlider *AudioMixerSettings::PCMVolume()
{
    HostSlider *gs = new HostSlider("PCMMixerVolume", 0, 100, 1);
    gs->setLabel(QObject::tr("PCM mixer volume"));
    gs->setValue(70);
    gs->setHelpText(QObject::tr("Initial volume for PCM output. Using the "
                    "volume keys in MythTV will adjust this parameter."));
    return gs;
}

AudioMixerSettings::AudioMixerSettings() :
    TriggeredConfigurationGroup(false, true, false, false)
{
    setLabel(QObject::tr("Audio Mixer"));
    setUseLabel(false);

    Setting *volumeControl = MythControlsVolume();
    addChild(volumeControl);

        // Mixer settings
    ConfigurationGroup *settings =
        new VerticalConfigurationGroup(false, true, false, false);
    settings->addChild(MixerDevice());
    settings->addChild(MixerControl());
    settings->addChild(MixerVolume());
    settings->addChild(PCMVolume());

    ConfigurationGroup *dummy =
        new VerticalConfigurationGroup(false, true, false, false);

        // Show Mixer config only if internal volume controls enabled
    setTrigger(volumeControl);
    addTarget("0", dummy);
    addTarget("1", settings);
}

AudioGeneralSettings::AudioGeneralSettings()
{
    addChild(new AudioConfigSettings(this));
    addChild(new AudioMixerSettings());
}

HostCheckBox *AudioAdvancedSettings::MPCM()
{
    HostCheckBox *gc = new HostCheckBox("StereoPCM");
    gc->setLabel(QObject::tr("Stereo PCM Only"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable if your amplifier or sound decoder "
                    "only supports 2 channels PCM (typically an old HDMI 1.0 "
                    "device). Multi-channels audio will be re-encoded to AC3 "
                    "when required"));
    return gc;
}

HostCheckBox *AudioAdvancedSettings::SRCQualityOverride()
{
    HostCheckBox *gc = new HostCheckBox("SRCQualityOverride");
    gc->setLabel(QObject::tr("Override SRC quality"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable to override audio sample rate "
                    "conversion quality."));
    return gc;
}

HostComboBox *AudioAdvancedSettings::SRCQuality()
{
    HostComboBox *gc = new HostComboBox("SRCQuality", false);
    gc->setLabel(QObject::tr("Sample rate conversion"));
    gc->addSelection(QObject::tr("Disabled"), "-1");
    gc->addSelection(QObject::tr("Fastest"), "0");
    gc->addSelection(QObject::tr("Good"), "1", true); // default
    gc->addSelection(QObject::tr("Best"), "2");
    gc->setHelpText(QObject::tr("Set the quality of audio sample-rate "
                    "conversion. \"Good\" (default) provides the best "
                    "compromise between CPU usage and quality. \"Disabled\" "
                    "lets the audio device handle sample-rate conversion."));
    return gc;
}

HostCheckBox *AudioAdvancedSettings::Audio48kOverride()
{
    HostCheckBox *gc = new HostCheckBox("Audio48kOverride");
    gc->setLabel(QObject::tr("Force audio device output to 48kHz"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Force audio sample rate to 48kHz. "
                                "Some audio devices will report various rates, "
                                "but they ultimately crash."));
    return gc;
}

HostCheckBox *AudioAdvancedSettings::PassThroughOverride()
{
    HostCheckBox *gc = new HostCheckBox("PassThruDeviceOverride");
    gc->setLabel(QObject::tr("Separate digital output device"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Use a distinct digital output device from "
                                "default."));
    return gc;
}

HostComboBox *AudioAdvancedSettings::PassThroughOutputDevice()
{
    HostComboBox *gc = new HostComboBox("PassThruOutputDevice", true);

    gc->setLabel(QObject::tr("Digital output device"));
    gc->addSelection(QObject::tr("Default"), "Default");
#ifdef USING_MINGW
    gc->addSelection("DirectX:Primary Sound Driver");
#else
    gc->addSelection("ALSA:iec958:{ AES0 0x02 }",
                     "ALSA:iec958:{ AES0 0x02 }");
    gc->addSelection("ALSA:hdmi", "ALSA:hdmi");
    gc->addSelection("ALSA:plughw:0,3", "ALSA:plughw:0,3");
#endif

    gc->setHelpText(QObject::tr("Audio output device to use for "
                        "digital audio. This value is currently only used "
                        "with ALSA and DirectX sound output."));
    return gc;
}

AudioAdvancedSettings::AudioAdvancedSettings(bool mpcm)
{
    ConfigurationGroup *settings3 =
        new HorizontalConfigurationGroup(false, false);

    m_PassThroughOverride = PassThroughOverride();
    TriggeredItem *sub3 =
        new TriggeredItem(m_PassThroughOverride, PassThroughOutputDevice());
    settings3->addChild(m_PassThroughOverride);
    settings3->addChild(sub3);

    ConfigurationGroup *settings4 =
        new HorizontalConfigurationGroup(false, false);
    Setting *srcqualityoverride = SRCQualityOverride();
    TriggeredItem *sub4 =
        new TriggeredItem(srcqualityoverride, SRCQuality());
    settings4->addChild(srcqualityoverride);
    settings4->addChild(sub4);

    ConfigurationGroup *settings5 =
        new HorizontalConfigurationGroup(false, false);
    settings5->addChild(Audio48kOverride());

    addChild(settings4);
    addChild(settings5);
    addChild(settings3);

    if (mpcm)
    {
        ConfigurationGroup *settings6;
        settings6 = new HorizontalConfigurationGroup(false, false);
        settings6->addChild(MPCM());
        addChild(settings6);
    }
}

AudioAdvancedSettingsGroup::AudioAdvancedSettingsGroup(bool mpcm)
{
    addChild(new AudioAdvancedSettings(mpcm));
}
// vim:set sw=4 ts=4 expandtab:
