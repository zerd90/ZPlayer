/* Link probe only: pulls one symbol from each vendored FFmpeg library.
 * Not playback logic — safe to remove once media code references the APIs. */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

volatile const void *zplayer_ffmpeg_link_probe(void)
{
    /* Prevent the linker from stripping unused archive members. */
    static const void *refs[] = {
        (const void *)avformat_version,
        (const void *)avcodec_version,
        (const void *)avutil_version,
        (const void *)swscale_version,
        (const void *)swresample_version,
    };
    return refs[0];
}
