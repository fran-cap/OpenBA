/*
 * third_party/wmadec — include/wmav2_decoder.h over FFmpeg's ff_wmav2_decoder.
 *
 * Does what libavcodec's avcodec_open2 / decode loop / avcodec_free_context do
 * for this one decoder, and nothing else: owns the AVCodecContext and the codec's
 * private context, implements ff_get_buffer over one growable set of float
 * planes, and hands each packet to the decoder from a zero-padded copy (FFmpeg's
 * bit reader may read up to AV_INPUT_BUFFER_PADDING_SIZE bytes past a packet;
 * the caller's packets sit back to back in a buffer whose end is not padded).
 * wmav2_drain adds the one thing FFmpeg's own EOF path does not do: it decodes
 * the frame left in the bit reservoir before returning the MDCT overlap, as
 * XAudio2 does (EXTRACT.md, "What XAudio2 2.7 does with an xWMA buffer").
 *
 * Banished reimplementation glue, not an FFmpeg file; see EXTRACT.md.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stddef.h>
#include <string.h>

#include "wmav2_decoder.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"
#include "libavcodec/decode.h"
#include "libavcodec/wma.h"

extern const FFCodec ff_wmav2_decoder;

struct Wmav2Decoder {
    AVCodecContext  avctx;          /* first: ff_get_buffer recovers the decoder from it */
    AVCodecInternal internal;
    AVFrame         frame;
    uint8_t         extradata[6 + AV_INPUT_BUFFER_PADDING_SIZE];
    float          *planes[2];
    int             plane_capacity; /* frames each plane holds */
    uint8_t        *packet;         /* block_align + padding, the padding zeroed */
};

void wmav2_xwma_extradata(uint8_t out[6])
{
    memset(out, 0, 6);
    out[4] = 31;                    /* libavformat/xwma.c: "our experimentally obtained value" */
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    Wmav2Decoder *dec = (Wmav2Decoder *)avctx;
    (void)flags;
    if (frame->nb_samples <= 0)
        return AVERROR(EINVAL);
    if (frame->nb_samples > dec->plane_capacity) {
        for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
            float *p = av_realloc_array(dec->planes[ch], frame->nb_samples, sizeof(float));
            if (!p)
                return AVERROR(ENOMEM);
            dec->planes[ch] = p;
        }
        dec->plane_capacity = frame->nb_samples;
    }
    frame->extended_data = (uint8_t **)dec->planes;
    return 0;
}

void wmav2_close(Wmav2Decoder *dec)
{
    if (!dec)
        return;
    if (dec->avctx.priv_data) {
        ff_wmav2_decoder.close(&dec->avctx);
        av_freep(&dec->avctx.priv_data);
    }
    av_freep(&dec->planes[0]);
    av_freep(&dec->planes[1]);
    av_freep(&dec->packet);
    av_free(dec);
}

int wmav2_open(const Wmav2Params *params, Wmav2Decoder **out)
{
    Wmav2Decoder *dec;
    int ret;

    if (!params || !out || params->channels < 1 || params->channels > 2 ||
        params->block_align <= 0 || params->extradata_size < 0 ||
        params->extradata_size > 6 || (params->extradata_size && !params->extradata))
        return AVERROR(EINVAL);
    /* avcodec_open2 rejects a negative sample rate before a codec's init runs;
     * this glue calls init directly, and ff_wma_init checks only the upper bound
     * (a negative rate overflowed `channels * sample_rate` at wma.c:140 and
     * shifted a negative value at wma.c:246; review F-071). Zero is refused too:
     * it divides in the same init. */
    if (params->sample_rate <= 0)
        return AVERROR(EINVAL);

    dec = av_mallocz(sizeof(*dec));
    if (!dec)
        return AVERROR(ENOMEM);

    dec->avctx.codec                = &ff_wmav2_decoder.p;
    dec->avctx.internal             = &dec->internal;
    dec->avctx.bit_rate             = params->bit_rate;
    dec->avctx.sample_rate          = params->sample_rate;
    dec->avctx.ch_layout.nb_channels = params->channels;
    dec->avctx.block_align          = params->block_align;
    if (params->extradata_size)
        memcpy(dec->extradata, params->extradata, params->extradata_size);
    dec->avctx.extradata            = dec->extradata;
    dec->avctx.extradata_size       = params->extradata_size;

    dec->packet = av_mallocz((size_t)params->block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    dec->avctx.priv_data = av_mallocz(ff_wmav2_decoder.priv_data_size);
    if (!dec->packet || !dec->avctx.priv_data) {
        wmav2_close(dec);
        return AVERROR(ENOMEM);
    }

    ret = ff_wmav2_decoder.init(&dec->avctx);
    if (ret < 0) {                  /* FF_CODEC_CAP_INIT_CLEANUP: close runs after a failed init */
        wmav2_close(dec);
        return ret;
    }
    *out = dec;
    return 0;
}

int wmav2_decode_packet(Wmav2Decoder *dec, const uint8_t *packet, int size,
                        const float *const **planes, int *samples)
{
    AVPacket pkt;
    int got = 0, ret;

    *samples = 0;
    *planes  = (const float *const *)dec->planes;
    if (!packet || size < dec->avctx.block_align)
        return AVERROR_INVALIDDATA;

    memcpy(dec->packet, packet, dec->avctx.block_align);
    pkt.data = dec->packet;
    pkt.size = dec->avctx.block_align;

    dec->frame.nb_samples = 0;
    ret = ff_wmav2_decoder.cb.decode(&dec->avctx, &dec->frame, &got, &pkt);
    if (ret < 0)
        return ret;
    if (got)
        *samples = dec->frame.nb_samples;
    return 0;
}

/* The end-of-stream terminator: a superframe that claims one frame and puts
 * zero new bits on it (header: 4-bit superframe index 0, 4-bit frame count 1,
 * a (byte_offset_bits + 3)-bit bit offset of 0, then zero padding). Fed to the
 * decoder's own packet path it completes the frame still waiting in the bit
 * reservoir with nothing, i.e. decodes that frame from the bits it already
 * holds; FFmpeg's EOF path (an empty packet) skips that frame and returns only
 * the MDCT overlap. XAudio2 2.7 decodes it (EXTRACT.md, rule 3). */
static int decode_reservoir_frame(Wmav2Decoder *dec, int *got)
{
    AVPacket pkt;
    memset(dec->packet, 0, (size_t)dec->avctx.block_align + AV_INPUT_BUFFER_PADDING_SIZE);
    dec->packet[0] = 0x01;          /* index 0 (high nibble), nb_frames 1 (low nibble) */
    pkt.data = dec->packet;
    pkt.size = dec->avctx.block_align;
    dec->frame.nb_samples = 0;
    return ff_wmav2_decoder.cb.decode(&dec->avctx, &dec->frame, got, &pkt);
}

int wmav2_drain(Wmav2Decoder *dec, const float *const **planes, int *samples)
{
    WMACodecContext *s = dec->avctx.priv_data;
    AVPacket pkt = { NULL, 0 };     /* an empty packet: wma_decode_superframe's EOF branch */
    int got = 0, ret;

    *samples = 0;
    *planes  = (const float *const *)dec->planes;
    if (s->use_bit_reservoir && s->last_superframe_len > 0 && !s->eof_done) {
        ret = decode_reservoir_frame(dec, &got);
        if (ret < 0)
            return ret;
        /* The decoder keeps the terminator's zero padding as the next reservoir;
         * it holds no frame, so the next call takes the EOF branch. */
        s->last_superframe_len = 0;
        if (got)
            *samples = dec->frame.nb_samples;
        return 0;
    }
    dec->frame.nb_samples = 0;
    ret = ff_wmav2_decoder.cb.decode(&dec->avctx, &dec->frame, &got, &pkt);
    if (ret < 0)
        return ret;
    if (got)
        *samples = dec->frame.nb_samples;
    return 0;
}

int wmav2_frame_len(const Wmav2Decoder *dec)
{
    const WMACodecContext *s = dec->avctx.priv_data;
    return s->frame_len;
}

void wmav2_reset(Wmav2Decoder *dec)
{
    ff_wmav2_decoder.flush(&dec->avctx);
}
