// MythTV
#include "mythlogging.h"
#include "mythmainwindow.h"
#include "osd.h"
#include "vulkan/mythwindowvulkan.h"
#include "vulkan/mythpaintervulkan.h"
#include "vulkan/mythdebugvulkan.h"
#include "vulkan/mythvideovulkan.h"
#include "vulkan/mythvideooutputvulkan.h"

#define LOC QString("VidOutVulkan: ")

VideoFrameTypeVec MythVideoOutputVulkan::s_vulkanFrameTypes =
{
    FMT_YV12,     FMT_NV12,      FMT_YUV422P,   FMT_YUV444P,
    FMT_YUV420P9, FMT_YUV420P10, FMT_YUV420P12, FMT_YUV420P14, FMT_YUV420P16,
    FMT_YUV422P9, FMT_YUV422P10, FMT_YUV422P12, FMT_YUV422P14, FMT_YUV422P16,
    FMT_YUV444P9, FMT_YUV444P10, FMT_YUV444P12, FMT_YUV444P14, FMT_YUV444P16,
    FMT_P010,     FMT_P016
};

void MythVideoOutputVulkan::GetRenderOptions(RenderOptions &Options)
{
    QStringList safe(VULKAN_RENDERER);

    (*Options.safe_renderers)["dummy"].append(safe);
    (*Options.safe_renderers)["ffmpeg"].append(safe);

    for (auto & decoder : *Options.decoders)
        if (decoder.endsWith("-dec"))
            (*Options.safe_renderers)[decoder].append(safe);

    Options.renderers->append(VULKAN_RENDERER);
    Options.priorities->insert(VULKAN_RENDERER, 75);
}

QStringList MythVideoOutputVulkan::GetAllowedRenderers(MythCodecID CodecId)
{
    QStringList allowed;

    if (MythRenderVulkan::GetVulkanRender() == nullptr)
        return allowed;

    if (codec_sw_copy(CodecId))
    {
        allowed << VULKAN_RENDERER;
        return allowed;
    }

    return allowed;
}

MythVideoOutputVulkan::MythVideoOutputVulkan(QString &Profile)
  : MythVideoOutputGPU(Profile),
    MythVulkanObject(MythRenderVulkan::GetVulkanRender())
{
    m_renderFrameTypes = &s_vulkanFrameTypes;

    m_render = MythVulkanObject::Render();
    // Note - strictly we shouldn't be using reference counting for MythRenderVulkan
    // as QVulkanWindow takes ownership. We need to ensure it is shared however,
    // otherwise the painter window is hidden by the incredibly annoying disabling of
    // drawing in MythMainWindow. There is no reason why QVulkanWindow should
    // delete it while video is playing though.
    if (m_render)
        m_render->IncrRef();

    if (IsValidVulkan())
        m_video = new MythVideoVulkan(this, &m_videoColourSpace, this, true, QString {});

    if (!(IsValidVulkan() && m_painter && m_video))
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to initialise Vulkan video output");
}

bool MythVideoOutputVulkan::Init(const QSize& VideoDim, const QSize& VideoDispDim,
                                 float Aspect, MythDisplay* Display,
                                 const QRect& DisplayVisibleRect, MythCodecID CodecId)
{
    if (!(IsValidVulkan() && m_painter && m_video))
        return false;

    if (!gCoreContext->IsUIThread())
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Cannot initialise from this thread");
        return false;
    }

    if (!InitGPU(VideoDim, VideoDispDim, Aspect, Display, DisplayVisibleRect, CodecId))
        return false;

    return true;
}

void MythVideoOutputVulkan::PrepareFrame(VideoFrame* Frame, const PIPMap& PiPPlayers, FrameScanType Scan)
{
    ProcessFrameGPU(Frame, PiPPlayers, Scan);
}

void MythVideoOutputVulkan::RenderFrame(VideoFrame* Frame, FrameScanType Scan, OSD* Osd)
{
    if (!(IsValidVulkan() && m_video))
        return;

    // input changes need to be handled in ProcessFrame
    if (m_newCodecId != kCodec_NONE)
        return;


    // FIXME GetWindowRect() is a placeholder
    QRect viewport = GetWindowRect();

    // Render preparation
    RenderFrameGPU(Frame, Scan, Osd, viewport, true);

    // Start the frame...
    m_video->StartFrame();

    VkCommandBuffer currentcmdbuffer = m_vulkanWindow->currentCommandBuffer();
    if (VERBOSE_LEVEL_CHECK(VB_GPU, LOG_INFO))
        m_vulkanRender->BeginDebugRegion(currentcmdbuffer, "PREPARE_FRAME", MythDebugVulkan::s_DebugBlue);

    // Actual render
    RenderFrameGPU(Frame, Scan, Osd, viewport);

    if (VERBOSE_LEVEL_CHECK(VB_GPU, LOG_INFO))
        m_vulkanRender->EndDebugRegion(currentcmdbuffer);
}

void MythVideoOutputVulkan::EndFrame()
{
    if (m_video)
        m_video->EndFrame();
}

MythVideoGPU* MythVideoOutputVulkan::CreateSecondaryVideo(const QSize& VideoDim,
                                                          const QSize& VideoDispDim,
                                                          const QRect& DisplayVisibleRect,
                                                          const QRect& DisplayVideoRect,
                                                          const QRect& VideoRect)
{
    auto * colourspace = new MythVideoColourSpace(&m_videoColourSpace);
    auto * result = new MythVideoVulkan(this, colourspace,
                                        VideoDim, VideoDispDim,
                                        DisplayVisibleRect, DisplayVideoRect,
                                        VideoRect, false, QString{});
    colourspace->DecrRef();
    if (result && !(result->IsValidVulkan() && result->IsValid()))
    {
        delete result;
        result = nullptr;
    }
    return result;
}
