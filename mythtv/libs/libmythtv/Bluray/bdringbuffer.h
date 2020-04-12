#ifndef BD_RING_BUFFER_H_
#define BD_RING_BUFFER_H_

// Qt
#include <QString>
#include <QRect>
#include <QHash>
#include <QImage>
#include <QCoreApplication>

// MythTV
#include "config.h"
#include "io/mythmediabuffer.h"

// BluRay
#include "libbluray/bluray.h"
#if CONFIG_LIBBLURAY_EXTERNAL
#include "libbluray/overlay.h"
#else
#include "libbluray/decoders/overlay.h"
#endif

#define BD_BLOCK_SIZE 6144LL

class MTV_PUBLIC MythBDInfo
{
    friend class MythBDBuffer;
    Q_DECLARE_TR_FUNCTIONS(MythBDInfo)

  public:
    explicit MythBDInfo(const QString &Filename);
   ~MythBDInfo(void) = default;
    bool    IsValid      (void) const;
    QString GetLastError (void) const;
    bool    GetNameAndSerialNum(QString &Name, QString &SerialNum);

  protected:
    static void GetNameAndSerialNum(BLURAY* BluRay, QString &Name,
                                    QString &SerialNum, const QString &Filename,
                                    const QString &LogPrefix);

  protected:
    QString  m_name;
    QString  m_serialnumber;
    QString  m_lastError;
    bool     m_isValid { true };
};

class MythBDOverlay
{
  public:
    MythBDOverlay() = default;
    explicit MythBDOverlay(const bd_overlay_s* Overlay);
    explicit MythBDOverlay(const bd_argb_overlay_s* Overlay);

    void     SetPalette(const BD_PG_PALETTE_ENTRY* Palette);
    void     Wipe(void);
    void     Wipe(int Left, int Top, int Width, int Height);

    QImage   m_image;
    int64_t  m_pts   { -1 };
    int      m_x     { 0  };
    int      m_y     { 0  };
};

/** \class MythBDBuffer
 *   A class to allow a MythMediaBuffer to read from BDs.
 */
class MTV_PUBLIC MythBDBuffer : public MythMediaBuffer
{
    Q_DECLARE_TR_FUNCTIONS(MythBDBuffer)

  public:
    explicit MythBDBuffer(const QString &Filename);
    ~MythBDBuffer() override;

    bool      IsStreamed         (void) override { return true; }
    void      IgnoreWaitStates   (bool Ignore) override;
    bool      StartFromBeginning (void) override;
    long long GetReadPosition    (void) const override;
    bool      IsOpen             (void) const override;
    bool      IsInMenu           (void) const override;
    bool      IsInStillFrame     (void) const override;
    bool      HandleAction       (const QStringList &Actions, int64_t Pts) override;
    bool      OpenFile           (const QString &Filename,
                                  uint Retry = static_cast<uint>(kDefaultOpenTimeout)) override;

    void      ProgressUpdate     (void);
    bool      BDWaitingForPlayer (void);
    void      SkipBDWaitingForPlayer(void);
    bool      GetNameAndSerialNum(QString& Name, QString& SerialNum);
    bool      GetBDStateSnapshot (QString& State);
    bool      RestoreBDStateSnapshot(const QString &State);
    void      ClearOverlays      (void);
    MythBDOverlay* GetOverlay    (void);
    void      SubmitOverlay      (const bd_overlay_s* Overlay);
    void      SubmitARGBOverlay  (const bd_argb_overlay_s* Overlay);
    uint32_t  GetNumTitles       (void) const;
    int       GetCurrentTitle    (void);
    uint64_t  GetCurrentAngle    (void) const;
    int       GetTitleDuration   (int Title);
    uint64_t  GetTitleSize       (void) const;
    uint64_t  GetTotalTimeOfTitle(void) const;
    uint64_t  GetCurrentTime     (void);
    uint64_t  GetTotalReadPosition(void);
    uint32_t  GetNumChapters     (void);
    uint32_t  GetCurrentChapter  (void);
    uint64_t  GetNumAngles       (void);
    uint64_t  GetChapterStartTime  (uint32_t Chapter);
    uint64_t  GetChapterStartFrame (uint32_t Chapter);
    bool      IsHDMVNavigation   (void) const;
    bool      TitleChanged       (void);
    bool      IsValidStream      (uint StreamId);
    void      UnblockReading     (void);
    bool      IsReadingBlocked   (void);
    int64_t   AdjustTimestamp    (int64_t Timestamp);
    void      GetDescForPos      (QString &Desc);
    double    GetFrameRate       (void);
    int       GetAudioLanguage   (uint StreamID);
    int       GetSubtitleLanguage(uint StreamID);
    void      Close              (void);
    bool      GoToMenu           (const QString &Menu, int64_t Pts);
    bool      SwitchTitle        (uint32_t Index);
    bool      SwitchPlaylist     (uint32_t Index);
    bool      SwitchAngle        (uint Angle);

  protected:
    int       SafeRead           (void *Buffer, uint Size) override;
    long long SeekInternal       (long long Position, int Whence) override;
    uint64_t  SeekInternal       (uint64_t Position);

  private:
    void      WaitForPlayer      (void);
    bool      UpdateTitleInfo    (void);
    BLURAY_TITLE_INFO* GetTitleInfo   (uint32_t Index);
    BLURAY_TITLE_INFO* GetPlaylistInfo(uint32_t Index);
    void      PressButton        (int32_t Key, int64_t Pts);
    void      ClickButton        (int64_t Pts, uint16_t X, uint16_t Y);
    bool      HandleBDEvents     (void);
    void      HandleBDEvent      (BD_EVENT &Event);

    static const BLURAY_STREAM_INFO* FindStream(uint StreamID,
                                                BLURAY_STREAM_INFO* Streams,
                                                int StreamCount);

    enum processState_t
    {
        PROCESS_NORMAL,
        PROCESS_REPROCESS,
        PROCESS_WAIT
    };

    BLURAY            *m_bdnav                       { nullptr };
    bool               m_isHDMVNavigation            { false   };
    bool               m_tryHDMVNavigation           { false   };
    bool               m_topMenuSupported            { false   };
    bool               m_firstPlaySupported          { false   };
    uint32_t           m_numTitles                   { 0       };
    uint32_t           m_mainTitle                   { 0       };
    uint64_t           m_currentTitleLength          { 0       };
    BLURAY_TITLE_INFO *m_currentTitleInfo            { nullptr };
    uint64_t           m_titlesize                   { 0       };
    uint64_t           m_currentTitleAngleCount      { 0       };
    uint64_t           m_currentTime                 { 0       };
    int                m_imgHandle                   { -1      };
    int                m_currentAngle                { 0       };
    int                m_currentTitle                { -1      };
    int                m_currentPlaylist             { 0       };
    int                m_currentPlayitem             { 0       };
    int                m_currentChapter              { 0       };
    int                m_currentAudioStream          { 0       };
    int                m_currentIGStream             { 0       };
    int                m_currentPGTextSTStream       { 0       };
    int                m_currentSecondaryAudioStream { 0       };
    int                m_currentSecondaryVideoStream { 0       };
    bool               m_pgTextSTEnabled             { false   };
    bool               m_secondaryAudioEnabled       { false   };
    bool               m_secondaryVideoEnabled       { false   };
    bool               m_secondaryVideoIsFullscreen  { false   };
    bool               m_titleChanged                { false   };
    bool               m_playerWait                  { false   };
    bool               m_ignorePlayerWait            { true    };
    QMutex             m_overlayLock;
    QList<MythBDOverlay*>   m_overlayImages;
    QVector<MythBDOverlay*> m_overlayPlanes;
    int                m_stillTime                   { 0       };
    int                m_stillMode                   { BLURAY_STILL_NONE};
    volatile bool      m_inMenu                      { false   };
    BD_EVENT           m_lastEvent                   { BD_EVENT_NONE, 0};
    processState_t     m_processState                { PROCESS_NORMAL};
    QByteArray         m_pendingData;
    int64_t            m_timeDiff                    { 0       };
    QHash<uint32_t,BLURAY_TITLE_INFO*> m_cachedTitleInfo;
    QHash<uint32_t,BLURAY_TITLE_INFO*> m_cachedPlaylistInfo;
    QMutex             m_infoLock                    { QMutex::Recursive };
    QString            m_name;
    QString            m_serialNumber;
    QThread           *m_mainThread                  { nullptr };
};
#endif
