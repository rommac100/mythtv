#ifndef MYTHVULKANVIDEOOUTPUT_H
#define MYTHVULKANVIDEOOUTPUT_H

// MythTV
#include "mythvideooutgpu.h"
#include "vulkan/mythrendervulkan.h"

class MythPainterVulkan;
class MythVideoVulkan;

#define VULKAN_RENDERER QString("vulkan")

class MythVideoOutputVulkan : public MythVideoOutputGPU, public MythVulkanObject
{
  public:
    static void        GetRenderOptions    (RenderOptions& Options);
    static QStringList GetAllowedRenderers (MythCodecID CodecId);
    static VideoFrameTypeVec s_vulkanFrameTypes;

    MythVideoOutputVulkan(QString& Profile);
   ~MythVideoOutputVulkan() override = default;

    bool IsPIPSupported           () const override { return false; }
    bool StereoscopicModesAllowed () const override { return false; }

    bool            Init(const QSize& VideoDim, const QSize& VideoDispDim, float Aspect,
                         MythDisplay* Display, const QRect& DisplayVisibleRect, MythCodecID CodecId) override;
    void            PrepareFrame (VideoFrame* Frame, const PIPMap& PiPPlayers, FrameScanType Scan) override;
    void            RenderFrame  (VideoFrame* Frame, FrameScanType Scan, OSD* Osd) override;
    void            EndFrame     () override;

  private:
    MythVideoGPU* CreateSecondaryVideo(const QSize& VideoDim,
                                       const QSize& VideoDispDim,
                                       const QRect& DisplayVisibleRect,
                                       const QRect& DisplayVideoRect,
                                       const QRect& VideoRect) override;
};

#endif
