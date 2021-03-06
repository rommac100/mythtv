// MythTV
#include "mythlogging.h"
#include "mythmainwindow.h"
#include "mythplayer.h"
#include "mythvideogpu.h"
#include "mythvideooutgpu.h"

#define LOC QString("VidOutGPU: ")

/*! \class MythVideoOutputGPU
 * \brief Common code shared between GPU accelerated sub-classes (e.g. OpenGL)
 *
 * MythVideoOutputGPU is a pure virtual class that contains code shared between
 * the differing hardware accelerated MythVideoOutput subclasses.
 *
 * \note This should be considered a work-in-progress and it is likely to change.
 *
 * \sa MythVideoOutput
 * \sa MythVideoOutputOpenGL
 * \sa MythVideoOutputVulkan
 */
MythVideoOutputGPU::MythVideoOutputGPU(QString& Profile)
  : m_profile(std::move(Profile))
{
    MythMainWindow* win = MythMainWindow::getMainWindow();
    if (win)
    {
        m_painter = win->GetCurrentPainter();
        if (m_painter)
            m_painter->SetMaster(false);
    }
}

MythVideoOutputGPU::~MythVideoOutputGPU()
{
    MythVideoOutputGPU::DestroyVisualisation();

    for (auto & pip : m_pxpVideos)
        delete pip;

    MythVideoOutputGPU::DestroyBuffers();
    delete m_video;
    if (m_painter)
        m_painter->SetMaster(true);
    if (m_render)
        m_render->DecrRef();
}

MythPainter* MythVideoOutputGPU::GetOSDPainter()
{
    return m_painter;
}

QRect MythVideoOutputGPU::GetDisplayVisibleRectAdj()
{
    return GetDisplayVisibleRect();
}

void MythVideoOutputGPU::InitPictureAttributes()
{
    m_videoColourSpace.SetSupportedAttributes(ALL_PICTURE_ATTRIBUTES);
}

void MythVideoOutputGPU::WindowResized(const QSize& Size)
{
    SetWindowSize(Size);
    InitDisplayMeasurements();
}

void MythVideoOutputGPU::SetVideoFrameRate(float NewRate)
{
    if (!m_dbDisplayProfile)
        return;

    if (qFuzzyCompare(m_dbDisplayProfile->GetOutput() + 1.0F, NewRate + 1.0F))
        return;

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Video frame rate changed: %1->%2")
        .arg(static_cast<double>(m_dbDisplayProfile->GetOutput())).arg(static_cast<double>(NewRate)));
    m_dbDisplayProfile->SetOutput(NewRate);
    m_newFrameRate = true;
}

bool MythVideoOutputGPU::InitGPU(const QSize& VideoDim, const QSize& VideoDispDim,
                                 float Aspect, MythDisplay* Display,
                                 const QRect& DisplayVisibleRect,
                                 MythCodecID CodecId)
{
    // if we are the main video player then free up as much video memory
    // as possible at startup
    PIPState pip = GetPIPState();
    if ((kCodec_NONE == m_newCodecId) && ((kPIPOff == pip) || (kPBPLeft == pip)) && m_painter)
        m_painter->FreeResources();

    // Default initialisation - mainly MythVideoBounds
    if (!MythVideoOutput::Init(VideoDim, VideoDispDim, Aspect, Display, DisplayVisibleRect, CodecId))
        return false;

    // Ensure any new profile preferences are handled after a stream change
    if (m_dbDisplayProfile)
        m_video->SetProfile(m_dbDisplayProfile->GetVideoRenderer());

    // Set default support for picture attributes
    InitPictureAttributes();

    // Setup display
    QSize size = GetVideoDim();

    // Set the display mode if required
    if (m_display->UsingVideoModes() && !IsEmbedding())
        ResizeForVideo(size);
    InitDisplayMeasurements();

    // Create buffers
    if (!CreateBuffers(CodecId, GetVideoDim()))
        return false;

    // Adjust visible rect for embedding
    QRect dvr = GetDisplayVisibleRectAdj();
    if (m_videoCodecID == kCodec_NONE)
    {
        m_render->SetViewPort(QRect(QPoint(), dvr.size()));
        return true;
    }

    if (GetPIPState() >= kPIPStandAlone)
    {
        QRect tmprect = QRect(QPoint(0,0), dvr.size());
        ResizeDisplayWindow(tmprect, true);
    }

    // Reset OpenGLVideo
    if (m_video->IsValid())
        m_video->ResetFrameFormat();

    return true;
}

/*! \brief Discard video frames
 *
 * If Flushed is true, the decoder will probably reset the hardware decoder in
 * use and we need to release any hardware pause frames so the decoder is released
 * before a new one is created.
*/
void MythVideoOutputGPU::DiscardFrames(bool KeyFrame, bool Flushed)
{
    if (Flushed)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("(%1): %2").arg(KeyFrame).arg(m_videoBuffers.GetStatus()));
        m_videoBuffers.DiscardPauseFrames();
    }
    MythVideoOutput::DiscardFrames(KeyFrame, Flushed);
}

/*! \brief Release a video frame back into the decoder pool.
 *
 * Software frames do not need a pause frame as the MythVideo subclass
 * holds a copy of the last frame in its input textures. So
 * just release the frame.
 *
 * Hardware frames hold the underlying interop class and
 * hence access to the video texture. We cannot access them
 * without a frame so retain the most recent frame by removing
 * it from the 'used' queue and adding it to the 'pause' queue.
*/
void MythVideoOutputGPU::DoneDisplayingFrame(VideoFrame* Frame)
{
    if (!Frame)
        return;

    bool retain = format_is_hw(Frame->codec);
    QVector<VideoFrame*> release;

    m_videoBuffers.BeginLock(kVideoBuffer_pause);
    while (m_videoBuffers.Size(kVideoBuffer_pause))
    {
        VideoFrame* frame = m_videoBuffers.Dequeue(kVideoBuffer_pause);
        if (!retain || (retain && (frame != Frame)))
            release.append(frame);
    }

    if (retain)
    {
        m_videoBuffers.Enqueue(kVideoBuffer_pause, Frame);
        if (m_videoBuffers.Contains(kVideoBuffer_used, Frame))
            m_videoBuffers.Remove(kVideoBuffer_used, Frame);
    }
    else
    {
        release.append(Frame);
    }
    m_videoBuffers.EndLock();

    for (auto * frame : release)
        m_videoBuffers.DoneDisplayingFrame(frame);
}

bool MythVideoOutputGPU::CreateBuffers(MythCodecID CodecID, QSize Size)
{
    if (m_buffersCreated)
        return true;

    if (codec_is_copyback(CodecID))
    {
        m_videoBuffers.Init(VideoBuffers::GetNumBuffers(FMT_NONE), false, 1, 4, 2);
        return m_videoBuffers.CreateBuffers(FMT_YV12, Size.width(), Size.height());
    }

    if (codec_is_mediacodec(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_MEDIACODEC, Size, false, 1, 2, 2);
    if (codec_is_vaapi(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VAAPI, Size, false, 2, 1, 4, m_maxReferenceFrames);
    if (codec_is_vtb(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VTB, Size, false, 1, 4, 2);
    if (codec_is_vdpau(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_VDPAU, Size, false, 2, 1, 4, m_maxReferenceFrames);
    if (codec_is_nvdec(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_NVDEC, Size, false, 2, 1, 4);
    if (codec_is_mmal(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_MMAL, Size, false, 2, 1, 4);
    if (codec_is_v4l2(CodecID) || codec_is_drmprime(CodecID))
        return m_videoBuffers.CreateBuffers(FMT_DRMPRIME, Size, false, 2, 1, 4);

    return m_videoBuffers.CreateBuffers(FMT_YV12, Size, false, 1, 8, 4, m_maxReferenceFrames);
}

void MythVideoOutputGPU::DestroyBuffers()
{
    MythVideoOutputGPU::DiscardFrames(true, true);
    m_videoBuffers.DeleteBuffers();
    m_videoBuffers.Reset();
    m_buffersCreated = false;
}

bool MythVideoOutputGPU::InputChanged(const QSize& VideoDim, const QSize& VideoDispDim,
                                      float Aspect, MythCodecID CodecId, bool& AspectOnly,
                                      MythMultiLocker* /*Locks*/, int ReferenceFrames,
                                      bool ForceChange)
{
    QSize currentvideodim     = GetVideoDim();
    QSize currentvideodispdim = GetVideoDispDim();
    MythCodecID currentcodec  = m_videoCodecID;
    float currentaspect       = GetVideoAspect();

    if (m_newCodecId != kCodec_NONE)
    {
        // InputChanged has been called twice in quick succession without a call to ProcessFrame
        currentvideodim = m_newVideoDim;
        currentvideodispdim = m_newVideoDispDim;
        currentcodec = m_newCodecId;
        currentaspect = m_newAspect;
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Video changed: %1x%2 (%3x%4) '%5' (Aspect %6 Refs %13)"
                                             "-> %7x%8 (%9x%10) '%11' (Aspect %12 Refs %14)")
        .arg(currentvideodispdim.width()).arg(currentvideodispdim.height())
        .arg(currentvideodim.width()).arg(currentvideodim.height())
        .arg(toString(currentcodec)).arg(static_cast<double>(currentaspect))
        .arg(VideoDispDim.width()).arg(VideoDispDim.height())
        .arg(VideoDim.width()).arg(VideoDim.height())
        .arg(toString(CodecId)).arg(static_cast<double>(Aspect))
        .arg(m_maxReferenceFrames).arg(ReferenceFrames));

    bool cidchanged = (CodecId != currentcodec);
    bool reschanged = (VideoDispDim != currentvideodispdim);
    bool refschanged = m_maxReferenceFrames != ReferenceFrames;

    // aspect ratio changes are a no-op as changes are handled at display time
    if (!(cidchanged || reschanged || refschanged || ForceChange))
    {
        AspectOnly = true;
        return true;
    }

    // N.B. We no longer check for interop support for the new codec as it is a
    // poor substitute for a full check of decoder capabilities etc. Better to let
    // hardware decoding fail if necessary - which should at least fallback to
    // software decoding rather than bailing out here.

    // delete and recreate the buffers and flag that the input has changed
    m_maxReferenceFrames = ReferenceFrames;
    m_buffersCreated = m_videoBuffers.DiscardAndRecreate(CodecId, VideoDim, m_maxReferenceFrames);
    if (!m_buffersCreated)
        return false;

    m_newCodecId= CodecId;
    m_newVideoDim = VideoDim;
    m_newVideoDispDim = VideoDispDim;
    m_newAspect = Aspect;
    return true;
}

bool MythVideoOutputGPU::ProcessInputChange()
{
    if (m_newCodecId != kCodec_NONE)
    {
        // Ensure we don't lose embedding through program changes.
        bool wasembedding = IsEmbedding();
        QRect oldrect;
        if (wasembedding)
        {
            oldrect = GetEmbeddingRect();
            StopEmbedding();
        }

        // Note - we don't call the default VideoOutput::InputChanged method as
        // the OpenGL implementation is asynchronous.
        // So we need to update the video display profile here. It is a little
        // circular as we need to set the video dimensions first which are then
        // reset in Init.
        // All told needs a cleanup - not least because the use of codecName appears
        // to be inconsistent.
        SourceChanged(m_newVideoDim, m_newVideoDispDim, m_newAspect);
        AVCodecID avCodecId = myth2av_codecid(m_newCodecId);
        AVCodec* codec = avcodec_find_decoder(avCodecId);
        QString codecName;
        if (codec)
            codecName = codec->name;
        if (m_dbDisplayProfile)
            m_dbDisplayProfile->SetInput(GetVideoDispDim(), 0 , codecName);

        bool ok = Init(m_newVideoDim, m_newVideoDispDim, m_newAspect,
                       m_display, GetDisplayVisibleRect(), m_newCodecId);
        m_newCodecId = kCodec_NONE;
        m_newVideoDim = QSize();
        m_newVideoDispDim = QSize();
        m_newAspect = 0.0F;
        m_newFrameRate = false;

        if (wasembedding && ok)
            EmbedInWidget(oldrect);

        if (!ok)
            return false;
    }
    else if (m_newFrameRate)
    {
        // If we are switching mode purely for a refresh rate change, then there
        // is no need to recreate buffers etc etc
        ResizeForVideo();
        m_newFrameRate = false;
    }

    return true;
}

/*! \brief Initialise display measurement
 *
 * The sole intent here is to ensure that MythVideoBounds has the correct aspect
 * ratio when it calculates the video display rectangle.
*/
void MythVideoOutputGPU::InitDisplayMeasurements()
{
    if (!m_display)
        return;

    // Retrieve the display aspect ratio.
    // This will be, in priority order:-
    // - aspect ratio override when using resolution/mode switching (if not 'Default')
    // - aspect ratio override for setups where detection does not work/is broken (multiscreen, broken EDID etc)
    // - aspect ratio based on detected physical size (this should be the common/default value)
    // - aspect ratio fallback using screen resolution
    // - 16:9
    QString source;
    double displayaspect = m_display->GetAspectRatio(source);
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Display aspect ratio: %1 (%2)")
        .arg(displayaspect).arg(source));

    // Get the window and screen resolutions
    QSize window = GetRawWindowRect().size();
    QSize screen = m_display->GetResolution();

    // If not running fullscreen, adjust for window size and ignore any video
    // mode overrides as they do not apply when in a window
    if (!window.isEmpty() && !screen.isEmpty() && window != screen)
    {
        displayaspect = m_display->GetAspectRatio(source, true);
        double screenaspect = screen.width() / static_cast<double>(screen.height());
        double windowaspect = window.width() / static_cast<double>(window.height());
        displayaspect = displayaspect * (1.0 / screenaspect) * windowaspect;
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Window aspect ratio: %1").arg(displayaspect));
    }

    SetDisplayAspect(static_cast<float>(displayaspect));
}

void MythVideoOutputGPU::ProcessFrameGPU(VideoFrame* Frame, const PIPMap &PiPPlayers, FrameScanType Scan)
{
    // Process input changes
    if (!ProcessInputChange())
        return;

    if (!IsEmbedding())
        ShowPIPs(PiPPlayers);

    if (Frame)
    {
        SetRotation(Frame->rotation);
        if (format_is_hw(Frame->codec) || Frame->dummy)
            return;

        // software deinterlacing
        m_deinterlacer.Filter(Frame, Scan, m_dbDisplayProfile);

        // update software textures
        if (m_video)
            m_video->PrepareFrame(Frame, Scan);
    }
}

void MythVideoOutputGPU::RenderFrameGPU(VideoFrame *Frame, FrameScanType Scan,
                                        OSD *Osd, const QRect& ViewPort, bool Prepare)
{
    // Stereoscopic views
    QRect view1 = ViewPort;
    QRect view2 = ViewPort;
    bool stereo = (m_stereo == kStereoscopicModeSideBySide) || (m_stereo == kStereoscopicModeTopAndBottom);

    if (kStereoscopicModeSideBySide == m_stereo)
    {
        view1 = QRect(ViewPort.left() / 2,  ViewPort.top(), ViewPort.width() / 2, ViewPort.height());
        view2 = view1.translated(ViewPort.width() / 2, 0);
    }
    else if (kStereoscopicModeTopAndBottom == m_stereo)
    {
        view1 = QRect(ViewPort.left(),  ViewPort.top() / 2, ViewPort.width(), ViewPort.height() / 2);
        view2 = view1.translated(0, ViewPort.height() / 2);
    }

    if (Prepare)
    {
        // Prepare visualisation
        if (m_visual && m_painter && m_visual->NeedsPrepare() && !IsEmbeddingAndHidden())
        {
            const QRect osdbounds = GetTotalOSDBounds();
            if (stereo)
                m_render->SetViewPort(view1, true);
            m_visual->Prepare(osdbounds);
            if (stereo)
            {
                m_render->SetViewPort(view2, true);
                m_visual->Prepare(osdbounds);
                m_render->SetViewPort(ViewPort);
            }
        }

        return;
    }

    bool dummy = false;
    bool topfieldfirst = false;
    if (Frame)
    {
        m_framesPlayed = Frame->frameNumber + 1;
        topfieldfirst = Frame->interlaced_reversed ? !Frame->top_field_first : Frame->top_field_first;
        dummy = Frame->dummy;
    }
    else
    {
        // see DoneDisplayingFrame
        // we only retain pause frames for hardware formats
        if (m_videoBuffers.Size(kVideoBuffer_pause))
            Frame = m_videoBuffers.Tail(kVideoBuffer_pause);
    }

    // Main UI when embedded
    if (IsEmbedding())
    {
        MythMainWindow* win = GetMythMainWindow();
        if (win && win->GetPaintWindow() && m_painter)
        {
            if (stereo)
                m_render->SetViewPort(view1, true);
            win->GetPaintWindow()->clearMask();
            win->Draw(m_painter);
            if (stereo)
            {
                m_render->SetViewPort(view2, true);
                win->GetPaintWindow()->clearMask();
                win->Draw(m_painter);
                m_render->SetViewPort(ViewPort, true);
            }
        }
    }

    // Video
    // N.B. dummy streams need the viewport updated in case we have resized the window (i.e. LiveTV)
    if (m_video && !dummy)
        m_video->RenderFrame(Frame, topfieldfirst, Scan, m_stereo);
    else if (dummy)
        m_render->SetViewPort(GetWindowRect());

    // PiPs/PBPs
    if (!m_pxpVideos.empty() && !IsEmbedding())
    {
        for (auto it = m_pxpVideos.begin(); it != m_pxpVideos.end(); ++it)
        {
            if (m_pxpVideosReady[it.key()] && (*it))
            {
                bool active = m_pxpVideoActive == *it;
                if (stereo)
                    m_render->SetViewPort(view1, true);
                (*it)->RenderFrame(nullptr, topfieldfirst, Scan, kStereoscopicModeNone, active);
                if (stereo)
                {
                    m_render->SetViewPort(view2, true);
                    (*it)->RenderFrame(nullptr, topfieldfirst, Scan, kStereoscopicModeNone, active);
                    m_render->SetViewPort(ViewPort);
                }
            }
        }
    }

    const QRect osdbounds = GetTotalOSDBounds();

    // Visualisation
    if (m_visual && m_painter && !IsEmbeddingAndHidden())
    {
        if (stereo)
            m_render->SetViewPort(view1, true);
        m_visual->Draw(osdbounds, m_painter, nullptr);
        if (stereo)
        {
            m_render->SetViewPort(view2, true);
            m_visual->Draw(osdbounds, m_painter, nullptr);
            m_render->SetViewPort(ViewPort);
        }
    }

    // OSD
    if (Osd && m_painter && !IsEmbedding())
    {
        if (stereo)
            m_render->SetViewPort(view1, true);
        Osd->Draw(m_painter, osdbounds.size(), true);
        if (stereo)
        {
            m_render->SetViewPort(view2, true);
            Osd->Draw(m_painter, osdbounds.size(), true);
            m_render->SetViewPort(ViewPort);
        }
    }
}

void MythVideoOutputGPU::UpdatePauseFrame(int64_t& DisplayTimecode, FrameScanType Scan)
{
    VideoFrame* release = nullptr;
    m_videoBuffers.BeginLock(kVideoBuffer_used);
    VideoFrame* used = m_videoBuffers.Head(kVideoBuffer_used);
    if (used)
    {
        if (format_is_hw(used->codec))
        {
            release = m_videoBuffers.Dequeue(kVideoBuffer_used);
        }
        else
        {
            Scan = (is_interlaced(Scan) && !used->already_deinterlaced) ? kScan_Interlaced : kScan_Progressive;
            m_deinterlacer.Filter(used, Scan, m_dbDisplayProfile, true);
            if (m_video)
                m_video->PrepareFrame(used, Scan);
        }
        DisplayTimecode = used->disp_timecode;
    }
    else
    {
        LOG(VB_PLAYBACK, LOG_WARNING, LOC + "Could not update pause frame");
    }
    m_videoBuffers.EndLock();

    if (release)
        DoneDisplayingFrame(release);
}

void MythVideoOutputGPU::EndFrame()
{
    m_video->EndFrame();
}

void MythVideoOutputGPU::ClearAfterSeek()
{
    // Clear reference frames for GPU deinterlacing
    if (m_video)
        m_video->ResetTextures();
    // Clear decoded frames
    MythVideoOutput::ClearAfterSeek();
}

bool MythVideoOutputGPU::EnableVisualisation(AudioPlayer* Audio, bool Enable, const QString& Name)
{
    if (!Enable)
    {
        DestroyVisualisation();
        return false;
    }
    return SetupVisualisation(Audio, Name);
}

QString MythVideoOutputGPU::GetVisualiserName()
{
    if (m_visual)
        return m_visual->Name();
    return MythVideoOutput::GetVisualiserName();
}

void MythVideoOutputGPU::DestroyVisualisation()
{
    delete m_visual;
    m_visual = nullptr;
}

bool MythVideoOutputGPU::StereoscopicModesAllowed() const
{
    return true;
}

void MythVideoOutputGPU::SetStereoscopicMode(StereoscopicMode Mode)
{
    m_stereo = Mode;
}

StereoscopicMode MythVideoOutputGPU::GetStereoscopicMode() const
{
    return m_stereo;
}

QStringList MythVideoOutputGPU::GetVisualiserList()
{
    if (m_render)
        return VideoVisual::GetVisualiserList(m_render->Type());
    return MythVideoOutput::GetVisualiserList();
}

bool MythVideoOutputGPU::CanVisualise(AudioPlayer* Audio)
{
    return VideoVisual::CanVisualise(Audio, m_render);
}

bool MythVideoOutputGPU::SetupVisualisation(AudioPlayer* Audio, const QString& Name)
{
    DestroyVisualisation();
    m_visual = VideoVisual::Create(Name, Audio, m_render);
    return m_visual != nullptr;
}

VideoVisual* MythVideoOutputGPU::GetVisualisation()
{
    return m_visual;
}

void MythVideoOutputGPU::ShowPIPs(const PIPMap& PiPPlayers)
{
    m_pxpVideoActive = nullptr;
    for (auto it = PiPPlayers.cbegin(); it != PiPPlayers.cend(); ++it)
        ShowPIP(it.key(), *it);
}

void MythVideoOutputGPU::ShowPIP(MythPlayer* PiPPlayer, PIPLocation Location)
{
    if (!PiPPlayer)
        return;

    int pipw = 0;
    int piph = 0;
    VideoFrame* pipimage     = PiPPlayer->GetCurrentFrame(pipw, piph);
    const QSize pipvideodim  = PiPPlayer->GetVideoBufferSize();
    QRect       pipvideorect = QRect(QPoint(0, 0), pipvideodim);

    if ((PiPPlayer->GetVideoAspect() <= 0.0F) || !pipimage || !pipimage->buf ||
        (pipimage->codec != FMT_YV12) || !PiPPlayer->IsPIPVisible())
    {
        PiPPlayer->ReleaseCurrentFrame(pipimage);
        return;
    }

    QRect position = GetPIPRect(Location, PiPPlayer);
    QRect dvr = GetDisplayVisibleRectAdj();

    m_pxpVideosReady[PiPPlayer] = false;
    MythVideoGPU* video = m_pxpVideos[PiPPlayer];

    if (video && video->GetVideoDim() != pipvideodim)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "Re-initialise PiP.");
        delete video;
        video = nullptr;
    }

    if (!video)
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + "Initialise PiP");
        video = CreateSecondaryVideo(pipvideodim, pipvideodim, dvr, position, pipvideorect);
    }

    m_pxpVideos[PiPPlayer] = video;

    if (video)
    {
        if (!video->IsValid())
        {
            PiPPlayer->ReleaseCurrentFrame(pipimage);
            return;
        }
        video->SetMasterViewport(dvr.size());
        video->SetVideoRects(position, pipvideorect);
        video->PrepareFrame(pipimage);
    }

    m_pxpVideosReady[PiPPlayer] = true;
    if (PiPPlayer->IsPIPActive())
        m_pxpVideoActive = video;
    PiPPlayer->ReleaseCurrentFrame(pipimage);
}

void MythVideoOutputGPU::RemovePIP(MythPlayer* PiPPlayer)
{
    if (m_pxpVideos.contains(PiPPlayer))
    {
        delete m_pxpVideos.take(PiPPlayer);
        m_pxpVideosReady.remove(PiPPlayer);
        m_pxpVideos.remove(PiPPlayer);
    }
}
