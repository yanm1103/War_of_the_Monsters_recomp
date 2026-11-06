#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *licenses[] = {avcodec_license(), avformat_license(), avutil_license(),
                              swresample_license(), swscale_license()};
    for (size_t i = 0; i < sizeof(licenses) / sizeof(licenses[0]); ++i) {
        puts(licenses[i]);
        if (!strstr(licenses[i], "LGPL") || strstr(licenses[i], "GNU General Public License"))
            return 1;
    }

    const char *config = avcodec_configuration();
    puts(config);
    if (strstr(config, "--enable-gpl") || strstr(config, "--enable-version3") ||
        strstr(config, "--enable-nonfree"))
        return 2;
    if (!avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO))
        return 3;

    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    if (!parser)
        return 4;
    av_parser_close(parser);

    struct SwsContext *sws = sws_getContext(16, 16, AV_PIX_FMT_YUV420P, 16, 16,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws)
        return 5;
    sws_freeContext(sws);
    return 0;
}
