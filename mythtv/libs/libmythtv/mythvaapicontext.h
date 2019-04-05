#ifndef MYTHVAAPICONTEXT_H
#define MYTHVAAPICONTEXT_H

// MythTV
#include "mythhwcontext.h"
#include "mythcodecid.h"

// VAAPI
#include "va/va.h"

// FFmpeg
extern "C" {
#include "libavutil/pixfmt.h"
#include "libavutil/hwcontext.h"
#include "libavcodec/avcodec.h"
}

class MythVAAPIContext
{
  public:
    static MythCodecID GetSupportedCodec (AVCodecContext *CodecContext,
                                          AVCodec       **Codec,
                                          const QString  &Decoder,
                                          uint            StreamType,
                                          AVPixelFormat  &PixFmt);
    static enum AVPixelFormat GetFormat  (struct AVCodecContext *Context,
                                          const enum AVPixelFormat *PixFmt);

  private:
    static int  InitialiseContext        (AVCodecContext *Context);
    static VAProfile VAAPIProfileForCodec(const AVCodecContext *Codec);
};

#endif // MYTHVAAPICONTEXT_H
