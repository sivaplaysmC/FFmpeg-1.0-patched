/*
 * Copyright (C) 2011-2012 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/opt.h"
#include "swresample_internal.h"
#include "audioconvert.h"
#include "libavutil/avassert.h"
#include "libavutil/audioconvert.h"

#include <float.h>

#define  C30DB  M_SQRT2
#define  C15DB  1.189207115
#define C__0DB  1.0
#define C_15DB  0.840896415
#define C_30DB  M_SQRT1_2
#define C_45DB  0.594603558
#define C_60DB  0.5

#define ALIGN 32

//TODO split options array out?
#define OFFSET(x) offsetof(SwrContext,x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM

static const AVOption options[]={
{"ich"                  ,  "Input Channel Count"        , OFFSET( in.ch_count   ), AV_OPT_TYPE_INT  , {.i64=2                     }, 0      , SWR_CH_MAX, PARAM},
{"in_channel_count"     ,  "Input Channel Count"        , OFFSET( in.ch_count   ), AV_OPT_TYPE_INT  , {.i64=2                     }, 0      , SWR_CH_MAX, PARAM},
{"och"                  , "Output Channel Count"        , OFFSET(out.ch_count   ), AV_OPT_TYPE_INT  , {.i64=2                     }, 0      , SWR_CH_MAX, PARAM},
{"out_channel_count"    , "Output Channel Count"        , OFFSET(out.ch_count   ), AV_OPT_TYPE_INT  , {.i64=2                     }, 0      , SWR_CH_MAX, PARAM},
{"uch"                  ,   "Used Channel Count"        , OFFSET(used_ch_count  ), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , SWR_CH_MAX, PARAM},
{"used_channel_count"   ,   "Used Channel Count"        , OFFSET(used_ch_count  ), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , SWR_CH_MAX, PARAM},
{"isr"                  ,  "Input Sample Rate"          , OFFSET( in_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"in_sample_rate"       ,  "Input Sample Rate"          , OFFSET( in_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"osr"                  , "Output Sample Rate"          , OFFSET(out_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"out_sample_rate"      , "Output Sample Rate"          , OFFSET(out_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"isf"                  ,    "Input Sample Format"      , OFFSET( in_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_NB-1+256, PARAM},
{"in_sample_fmt"        ,    "Input Sample Format"      , OFFSET( in_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_NB-1+256, PARAM},
{"osf"                  ,   "Output Sample Format"      , OFFSET(out_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_NB-1+256, PARAM},
{"out_sample_fmt"       ,   "Output Sample Format"      , OFFSET(out_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_NB-1+256, PARAM},
{"tsf"                  , "Internal Sample Format"      , OFFSET(int_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_FLTP, PARAM},
{"internal_sample_fmt"  , "Internal Sample Format"      , OFFSET(int_sample_fmt ), AV_OPT_TYPE_INT  , {.i64=AV_SAMPLE_FMT_NONE    }, -1     , AV_SAMPLE_FMT_FLTP, PARAM},
{"icl"                  ,   "Input Channel Layout"      , OFFSET( in_ch_layout  ), AV_OPT_TYPE_INT64, {.i64=0                     }, 0      , INT64_MAX , PARAM, "channel_layout"},
{"in_channel_layout"    ,   "Input Channel Layout"      , OFFSET( in_ch_layout  ), AV_OPT_TYPE_INT64, {.i64=0                     }, 0      , INT64_MAX , PARAM, "channel_layout"},
{"ocl"                  ,  "Output Channel Layout"      , OFFSET(out_ch_layout  ), AV_OPT_TYPE_INT64, {.i64=0                     }, 0      , INT64_MAX , PARAM, "channel_layout"},
{"out_channel_layout"   ,  "Output Channel Layout"      , OFFSET(out_ch_layout  ), AV_OPT_TYPE_INT64, {.i64=0                     }, 0      , INT64_MAX , PARAM, "channel_layout"},
{"clev"                 ,    "Center Mix Level"         , OFFSET(clev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"center_mix_level"     ,    "Center Mix Level"         , OFFSET(clev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"slev"                 , "Sourround Mix Level"         , OFFSET(slev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"surround_mix_level"   , "Sourround Mix Level"         , OFFSET(slev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"lfe_mix_level"        , "LFE Mix Level"               , OFFSET(lfe_mix_level  ), AV_OPT_TYPE_FLOAT, {.dbl=0                     }, -32    , 32        , PARAM},
{"rmvol"                , "Rematrix Volume"             , OFFSET(rematrix_volume), AV_OPT_TYPE_FLOAT, {.dbl=1.0                   }, -1000  , 1000      , PARAM},
{"rematrix_volume"      , "Rematrix Volume"             , OFFSET(rematrix_volume), AV_OPT_TYPE_FLOAT, {.dbl=1.0                   }, -1000  , 1000      , PARAM},
{"flags"                , NULL                          , OFFSET(flags          ), AV_OPT_TYPE_FLAGS, {.i64=0                     }, 0      , UINT_MAX  , PARAM, "flags"},
{"swr_flags"            , NULL                          , OFFSET(flags          ), AV_OPT_TYPE_FLAGS, {.i64=0                     }, 0      , UINT_MAX  , PARAM, "flags"},
{"res"                  , "Force Resampling"            , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_FLAG_RESAMPLE     }, INT_MIN, INT_MAX   , PARAM, "flags"},
{"dither_scale"         , "Dither Scale"                , OFFSET(dither_scale   ), AV_OPT_TYPE_FLOAT, {.dbl=1                     }, 0      , INT_MAX   , PARAM},
{"dither_method"        , "Dither Method"               , OFFSET(dither_method  ), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , SWR_DITHER_NB-1, PARAM, "dither_method"},
{"rectangular"          , "Rectangular Dither"          , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_RECTANGULAR}, INT_MIN, INT_MAX   , PARAM, "dither_method"},
{"triangular"           ,  "Triangular Dither"          , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_TRIANGULAR }, INT_MIN, INT_MAX   , PARAM, "dither_method"},
{"triangular_hp"        , "Triangular Dither With High Pass" , 0                 , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_TRIANGULAR_HIGHPASS }, INT_MIN, INT_MAX, PARAM, "dither_method"},
{"filter_size"          , "Resampling Filter Size"      , OFFSET(filter_size)    , AV_OPT_TYPE_INT  , {.i64=16                    }, 0      , INT_MAX   , PARAM },
{"phase_shift"          , "Resampling Phase Shift"      , OFFSET(phase_shift)    , AV_OPT_TYPE_INT  , {.i64=10                    }, 0      , 30        , PARAM },
{"linear_interp"        , "Use Linear Interpolation"    , OFFSET(linear_interp)  , AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , 1         , PARAM },
{"cutoff"               , "Cutoff Frequency Ratio"      , OFFSET(cutoff)         , AV_OPT_TYPE_DOUBLE,{.dbl=0.8                   }, 0      , 1         , PARAM },
{"min_comp"             , "Minimum difference between timestamps and audio data (in seconds) below which no timestamp compensation of either kind is applied"
                                                        , OFFSET(min_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=FLT_MAX               }, 0      , FLT_MAX   , PARAM },
{"min_hard_comp"        , "Minimum difference between timestamps and audio data (in seconds) to trigger padding/trimming the data."
                                                   , OFFSET(min_hard_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=0.1                   }, 0      , INT_MAX   , PARAM },
{"comp_duration"        , "Duration (in seconds) over which data is stretched/squeezed to make it match the timestamps."
                                              , OFFSET(soft_compensation_duration),AV_OPT_TYPE_FLOAT ,{.dbl=1                     }, 0      , INT_MAX   , PARAM },
{"max_soft_comp"        , "Maximum factor by which data is stretched/squeezed to make it match the timestamps."
                                                   , OFFSET(max_soft_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=0                     }, INT_MIN, INT_MAX   , PARAM },
{ "matrix_encoding"     , "Matrixed Stereo Encoding"    , OFFSET(matrix_encoding), AV_OPT_TYPE_INT   ,{.i64 = AV_MATRIX_ENCODING_NONE}, AV_MATRIX_ENCODING_NONE,     AV_MATRIX_ENCODING_NB-1, PARAM, "matrix_encoding" },
    { "none",  "None",               0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_NONE  }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
    { "dolby", "Dolby",              0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DOLBY }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
    { "dplii", "Dolby Pro Logic II", 0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DPLII }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
{ "filter_type"         , "Filter Type"                 , OFFSET(filter_type)    , AV_OPT_TYPE_INT  , { .i64 = SWR_FILTER_TYPE_KAISER }, SWR_FILTER_TYPE_CUBIC, SWR_FILTER_TYPE_KAISER, PARAM, "filter_type" },
    { "cubic"           , "Cubic"                       , 0                      , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_CUBIC            }, INT_MIN, INT_MAX, PARAM, "filter_type" },
    { "blackman_nuttall", "Blackman Nuttall Windowed Sinc", 0                    , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_BLACKMAN_NUTTALL }, INT_MIN, INT_MAX, PARAM, "filter_type" },
    { "kaiser"          , "Kaiser Windowed Sinc"        , 0                      , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_KAISER           }, INT_MIN, INT_MAX, PARAM, "filter_type" },
{ "kaiser_beta"         , "Kaiser Window Beta"          ,OFFSET(kaiser_beta)     , AV_OPT_TYPE_INT  , {.i64=9                     }, 2      , 16        , PARAM },

{0}
};

static const char* context_to_name(void* ptr) {
    return "SWR";
}

static const AVClass av_class = {
    .class_name                = "SWResampler",
    .item_name                 = context_to_name,
    .option                    = options,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = OFFSET(log_level_offset),
    .parent_log_context_offset = OFFSET(log_ctx),
    .category                  = AV_CLASS_CATEGORY_SWRESAMPLER,
};

unsigned swresample_version(void)
{
    av_assert0(LIBSWRESAMPLE_VERSION_MICRO >= 100);
    return LIBSWRESAMPLE_VERSION_INT;
}

const char *swresample_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *swresample_license(void)
{
#define LICENSE_PREFIX "libswresample license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

int swr_set_channel_mapping(struct SwrContext *s, const int *channel_map){
    if(!s || s->in_convert) // s needs to be allocated but not initialized
        return AVERROR(EINVAL);
    s->channel_map = channel_map;
    return 0;
}

const AVClass *swr_get_class(void)
{
    return &av_class;
}

av_cold struct SwrContext *swr_alloc(void){
    SwrContext *s= av_mallocz(sizeof(SwrContext));
    if(s){
        s->av_class= &av_class;
        av_opt_set_defaults(s);
    }
    return s;
}

struct SwrContext *swr_alloc_set_opts(struct SwrContext *s,
                                      int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                                      int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
                                      int log_offset, void *log_ctx){
    if(!s) s= swr_alloc();
    if(!s) return NULL;

    s->log_level_offset= log_offset;
    s->log_ctx= log_ctx;

    av_opt_set_int(s, "ocl", out_ch_layout,   0);
    av_opt_set_int(s, "osf", out_sample_fmt,  0);
    av_opt_set_int(s, "osr", out_sample_rate, 0);
    av_opt_set_int(s, "icl", in_ch_layout,    0);
    av_opt_set_int(s, "isf", in_sample_fmt,   0);
    av_opt_set_int(s, "isr", in_sample_rate,  0);
    av_opt_set_int(s, "tsf", AV_SAMPLE_FMT_NONE,   0);
    av_opt_set_int(s, "ich", av_get_channel_layout_nb_channels(s-> in_ch_layout), 0);
    av_opt_set_int(s, "och", av_get_channel_layout_nb_channels(s->out_ch_layout), 0);
    av_opt_set_int(s, "uch", 0, 0);
    return s;
}

static void set_audiodata_fmt(AudioData *a, enum AVSampleFormat fmt){
    a->fmt   = fmt;
    a->bps   = av_get_bytes_per_sample(fmt);
    a->planar= av_sample_fmt_is_planar(fmt);
}

static void free_temp(AudioData *a){
    av_free(a->data);
    memset(a, 0, sizeof(*a));
}

av_cold void swr_free(SwrContext **ss){
    SwrContext *s= *ss;
    if(s){
        free_temp(&s->postin);
        free_temp(&s->midbuf);
        free_temp(&s->preout);
        free_temp(&s->in_buffer);
        free_temp(&s->dither);
        swri_audio_convert_free(&s-> in_convert);
        swri_audio_convert_free(&s->out_convert);
        swri_audio_convert_free(&s->full_convert);
        swri_resample_free(&s->resample);
        swri_rematrix_free(s);
    }

    av_freep(ss);
}

av_cold int swr_init(struct SwrContext *s){
    s->in_buffer_index= 0;
    s->in_buffer_count= 0;
    s->resample_in_constraint= 0;
    free_temp(&s->postin);
    free_temp(&s->midbuf);
    free_temp(&s->preout);
    free_temp(&s->in_buffer);
    free_temp(&s->dither);
    memset(s->in.ch, 0, sizeof(s->in.ch));
    memset(s->out.ch, 0, sizeof(s->out.ch));
    swri_audio_convert_free(&s-> in_convert);
    swri_audio_convert_free(&s->out_convert);
    swri_audio_convert_free(&s->full_convert);
    swri_rematrix_free(s);

    s->flushed = 0;

    if(av_get_channel_layout_nb_channels(s-> in_ch_layout) > SWR_CH_MAX) {
        av_log(s, AV_LOG_WARNING, "Input channel layout 0x%"PRIx64" is invalid or unsupported.\n", s-> in_ch_layout);
        s->in_ch_layout = 0;
    }

    if(av_get_channel_layout_nb_channels(s->out_ch_layout) > SWR_CH_MAX) {
        av_log(s, AV_LOG_WARNING, "Output channel layout 0x%"PRIx64" is invalid or unsupported.\n", s->out_ch_layout);
        s->out_ch_layout = 0;
    }

    if(s-> in_sample_fmt >= AV_SAMPLE_FMT_NB){
        av_log(s, AV_LOG_ERROR, "Requested input sample format %d is invalid\n", s->in_sample_fmt);
        return AVERROR(EINVAL);
    }
    if(s->out_sample_fmt >= AV_SAMPLE_FMT_NB){
        av_log(s, AV_LOG_ERROR, "Requested output sample format %d is invalid\n", s->out_sample_fmt);
        return AVERROR(EINVAL);
    }

    if(s->int_sample_fmt == AV_SAMPLE_FMT_NONE){
        if(av_get_planar_sample_fmt(s->in_sample_fmt) <= AV_SAMPLE_FMT_S16P){
            s->int_sample_fmt= AV_SAMPLE_FMT_S16P;
        }else if(av_get_planar_sample_fmt(s->in_sample_fmt) <= AV_SAMPLE_FMT_FLTP){
            s->int_sample_fmt= AV_SAMPLE_FMT_FLTP;
        }else{
            av_log(s, AV_LOG_DEBUG, "Using double precision mode\n");
            s->int_sample_fmt= AV_SAMPLE_FMT_DBLP;
        }
    }

    if(   s->int_sample_fmt != AV_SAMPLE_FMT_S16P
        &&s->int_sample_fmt != AV_SAMPLE_FMT_S32P
        &&s->int_sample_fmt != AV_SAMPLE_FMT_FLTP
        &&s->int_sample_fmt != AV_SAMPLE_FMT_DBLP){
        av_log(s, AV_LOG_ERROR, "Requested sample format %s is not supported internally, S16/S32/FLT/DBL is supported\n", av_get_sample_fmt_name(s->int_sample_fmt));
        return AVERROR(EINVAL);
    }

    set_audiodata_fmt(&s-> in, s-> in_sample_fmt);
    set_audiodata_fmt(&s->out, s->out_sample_fmt);

    if (s->out_sample_rate!=s->in_sample_rate || (s->flags & SWR_FLAG_RESAMPLE)){
        s->resample = swri_resample_init(s->resample, s->out_sample_rate, s->in_sample_rate, s->filter_size, s->phase_shift, s->linear_interp, s->cutoff, s->int_sample_fmt, s->filter_type, s->kaiser_beta);
    }else
        swri_resample_free(&s->resample);
    if(    s->int_sample_fmt != AV_SAMPLE_FMT_S16P
        && s->int_sample_fmt != AV_SAMPLE_FMT_S32P
        && s->int_sample_fmt != AV_SAMPLE_FMT_FLTP
        && s->int_sample_fmt != AV_SAMPLE_FMT_DBLP
        && s->resample){
        av_log(s, AV_LOG_ERROR, "Resampling only supported with internal s16/s32/flt/dbl\n");
        return -1;
    }

    if(!s->used_ch_count)
        s->used_ch_count= s->in.ch_count;

    if(s->used_ch_count && s-> in_ch_layout && s->used_ch_count != av_get_channel_layout_nb_channels(s-> in_ch_layout)){
        av_log(s, AV_LOG_WARNING, "Input channel layout has a different number of channels than the number of used channels, ignoring layout\n");
        s-> in_ch_layout= 0;
    }

    if(!s-> in_ch_layout)
        s-> in_ch_layout= av_get_default_channel_layout(s->used_ch_count);
    if(!s->out_ch_layout)
        s->out_ch_layout= av_get_default_channel_layout(s->out.ch_count);

    s->rematrix= s->out_ch_layout  !=s->in_ch_layout || s->rematrix_volume!=1.0 ||
                 s->rematrix_custom;

#define RSC 1 //FIXME finetune
    if(!s-> in.ch_count)
        s-> in.ch_count= av_get_channel_layout_nb_channels(s-> in_ch_layout);
    if(!s->used_ch_count)
        s->used_ch_count= s->in.ch_count;
    if(!s->out.ch_count)
        s->out.ch_count= av_get_channel_layout_nb_channels(s->out_ch_layout);

    if(!s-> in.ch_count){
        av_assert0(!s->in_ch_layout);
        av_log(s, AV_LOG_ERROR, "Input channel count and layout are unset\n");
        return -1;
    }

    if ((!s->out_ch_layout || !s->in_ch_layout) && s->used_ch_count != s->out.ch_count && !s->rematrix_custom) {
        av_log(s, AV_LOG_ERROR, "Rematrix is needed but there is not enough information to do it\n");
        return -1;
    }

av_assert0(s->used_ch_count);
av_assert0(s->out.ch_count);
    s->resample_first= RSC*s->out.ch_count/s->in.ch_count - RSC < s->out_sample_rate/(float)s-> in_sample_rate - 1.0;

    s->in_buffer= s->in;

    if(!s->resample && !s->rematrix && !s->channel_map && !s->dither_method){
        s->full_convert = swri_audio_convert_alloc(s->out_sample_fmt,
                                                   s-> in_sample_fmt, s-> in.ch_count, NULL, 0);
        return 0;
    }

    s->in_convert = swri_audio_convert_alloc(s->int_sample_fmt,
                                             s-> in_sample_fmt, s->used_ch_count, s->channel_map, 0);
    s->out_convert= swri_audio_convert_alloc(s->out_sample_fmt,
                                             s->int_sample_fmt, s->out.ch_count, NULL, 0);


    s->postin= s->in;
    s->preout= s->out;
    s->midbuf= s->in;

    if(s->channel_map){
        s->postin.ch_count=
        s->midbuf.ch_count= s->used_ch_count;
        if(s->resample)
            s->in_buffer.ch_count= s->used_ch_count;
    }
    if(!s->resample_first){
        s->midbuf.ch_count= s->out.ch_count;
        if(s->resample)
            s->in_buffer.ch_count = s->out.ch_count;
    }

    set_audiodata_fmt(&s->postin, s->int_sample_fmt);
    set_audiodata_fmt(&s->midbuf, s->int_sample_fmt);
    set_audiodata_fmt(&s->preout, s->int_sample_fmt);

    if(s->resample){
        set_audiodata_fmt(&s->in_buffer, s->int_sample_fmt);
    }

    s->dither = s->preout;

    if(s->rematrix || s->dither_method)
        return swri_rematrix_init(s);

    return 0;
}

static int realloc_audio(AudioData *a, int count){
    int i, countb;
    AudioData old;

    if(count < 0 || count > INT_MAX/2/a->bps/a->ch_count)
        return AVERROR(EINVAL);

    if(a->count >= count)
        return 0;

    count*=2;

    countb= FFALIGN(count*a->bps, ALIGN);
    old= *a;

    av_assert0(a->bps);
    av_assert0(a->ch_count);

    a->data= av_mallocz(countb*a->ch_count);
    if(!a->data)
        return AVERROR(ENOMEM);
    for(i=0; i<a->ch_count; i++){
        a->ch[i]= a->data + i*(a->planar ? countb : a->bps);
        if(a->planar) memcpy(a->ch[i], old.ch[i], a->count*a->bps);
    }
    if(!a->planar) memcpy(a->ch[0], old.ch[0], a->count*a->ch_count*a->bps);
    av_free(old.data);
    a->count= count;

    return 1;
}

static void copy(AudioData *out, AudioData *in,
                 int count){
    av_assert0(out->planar == in->planar);
    av_assert0(out->bps == in->bps);
    av_assert0(out->ch_count == in->ch_count);
    if(out->planar){
        int ch;
        for(ch=0; ch<out->ch_count; ch++)
            memcpy(out->ch[ch], in->ch[ch], count*out->bps);
    }else
        memcpy(out->ch[0], in->ch[0], count*out->ch_count*out->bps);
}

static void fill_audiodata(AudioData *out, uint8_t *in_arg [SWR_CH_MAX]){
    int i;
    if(!in_arg){
        memset(out->ch, 0, sizeof(out->ch));
    }else if(out->planar){
        for(i=0; i<out->ch_count; i++)
            out->ch[i]= in_arg[i];
    }else{
        for(i=0; i<out->ch_count; i++)
            out->ch[i]= in_arg[0] + i*out->bps;
    }
}

static void reversefill_audiodata(AudioData *out, uint8_t *in_arg [SWR_CH_MAX]){
    int i;
    if(out->planar){
        for(i=0; i<out->ch_count; i++)
            in_arg[i]= out->ch[i];
    }else{
        in_arg[0]= out->ch[0];
    }
}

/**
 *
 * out may be equal in.
 */
static void buf_set(AudioData *out, AudioData *in, int count){
    int ch;
    if(in->planar){
        for(ch=0; ch<out->ch_count; ch++)
            out->ch[ch]= in->ch[ch] + count*out->bps;
    }else{
        for(ch=out->ch_count-1; ch>=0; ch--)
            out->ch[ch]= in->ch[0] + (ch + count*out->ch_count) * out->bps;
    }
}

/**
 *
 * @return number of samples output per channel
 */
static int resample(SwrContext *s, AudioData *out_param, int out_count,
                             const AudioData * in_param, int in_count){
    AudioData in, out, tmp;
    int ret_sum=0;
    int border=0;

    av_assert1(s->in_buffer.ch_count == in_param->ch_count);
    av_assert1(s->in_buffer.planar   == in_param->planar);
    av_assert1(s->in_buffer.fmt      == in_param->fmt);

    tmp=out=*out_param;
    in =  *in_param;

    do{
        int ret, size, consumed;
        if(!s->resample_in_constraint && s->in_buffer_count){
            buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
            ret= swri_multiple_resample(s->resample, &out, out_count, &tmp, s->in_buffer_count, &consumed);
            out_count -= ret;
            ret_sum += ret;
            buf_set(&out, &out, ret);
            s->in_buffer_count -= consumed;
            s->in_buffer_index += consumed;

            if(!in_count)
                break;
            if(s->in_buffer_count <= border){
                buf_set(&in, &in, -s->in_buffer_count);
                in_count += s->in_buffer_count;
                s->in_buffer_count=0;
                s->in_buffer_index=0;
                border = 0;
            }
        }

        if(in_count && !s->in_buffer_count){
            s->in_buffer_index=0;
            ret= swri_multiple_resample(s->resample, &out, out_count, &in, in_count, &consumed);
            out_count -= ret;
            ret_sum += ret;
            buf_set(&out, &out, ret);
            in_count -= consumed;
            buf_set(&in, &in, consumed);
        }

        //TODO is this check sane considering the advanced copy avoidance below
        size= s->in_buffer_index + s->in_buffer_count + in_count;
        if(   size > s->in_buffer.count
           && s->in_buffer_count + in_count <= s->in_buffer_index){
            buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
            copy(&s->in_buffer, &tmp, s->in_buffer_count);
            s->in_buffer_index=0;
        }else
            if((ret=realloc_audio(&s->in_buffer, size)) < 0)
                return ret;

        if(in_count){
            int count= in_count;
            if(s->in_buffer_count && s->in_buffer_count+2 < count && out_count) count= s->in_buffer_count+2;

            buf_set(&tmp, &s->in_buffer, s->in_buffer_index + s->in_buffer_count);
            copy(&tmp, &in, /*in_*/count);
            s->in_buffer_count += count;
            in_count -= count;
            border += count;
            buf_set(&in, &in, count);
            s->resample_in_constraint= 0;
            if(s->in_buffer_count != count || in_count)
                continue;
        }
        break;
    }while(1);

    s->resample_in_constraint= !!out_count;

    return ret_sum;
}

static int swr_convert_internal(struct SwrContext *s, AudioData *out, int out_count,
                                                      AudioData *in , int  in_count){
    AudioData *postin, *midbuf, *preout;
    int ret/*, in_max*/;
    AudioData preout_tmp, midbuf_tmp;

    if(s->full_convert){
        av_assert0(!s->resample);
        swri_audio_convert(s->full_convert, out, in, in_count);
        return out_count;
    }

//     in_max= out_count*(int64_t)s->in_sample_rate / s->out_sample_rate + resample_filter_taps;
//     in_count= FFMIN(in_count, in_in + 2 - s->hist_buffer_count);

    if((ret=realloc_audio(&s->postin, in_count))<0)
        return ret;
    if(s->resample_first){
        av_assert0(s->midbuf.ch_count == s->used_ch_count);
        if((ret=realloc_audio(&s->midbuf, out_count))<0)
            return ret;
    }else{
        av_assert0(s->midbuf.ch_count ==  s->out.ch_count);
        if((ret=realloc_audio(&s->midbuf,  in_count))<0)
            return ret;
    }
    if((ret=realloc_audio(&s->preout, out_count))<0)
        return ret;

    postin= &s->postin;

    midbuf_tmp= s->midbuf;
    midbuf= &midbuf_tmp;
    preout_tmp= s->preout;
    preout= &preout_tmp;

    if(s->int_sample_fmt == s-> in_sample_fmt && s->in.planar && !s->channel_map)
        postin= in;

    if(s->resample_first ? !s->resample : !s->rematrix)
        midbuf= postin;

    if(s->resample_first ? !s->rematrix : !s->resample)
        preout= midbuf;

    if(s->int_sample_fmt == s->out_sample_fmt && s->out.planar){
        if(preout==in){
            out_count= FFMIN(out_count, in_count); //TODO check at the end if this is needed or redundant
            av_assert0(s->in.planar); //we only support planar internally so it has to be, we support copying non planar though
            copy(out, in, out_count);
            return out_count;
        }
        else if(preout==postin) preout= midbuf= postin= out;
        else if(preout==midbuf) preout= midbuf= out;
        else                    preout= out;
    }

    if(in != postin){
        swri_audio_convert(s->in_convert, postin, in, in_count);
    }

    if(s->resample_first){
        if(postin != midbuf)
            out_count= resample(s, midbuf, out_count, postin, in_count);
        if(midbuf != preout)
            swri_rematrix(s, preout, midbuf, out_count, preout==out);
    }else{
        if(postin != midbuf)
            swri_rematrix(s, midbuf, postin, in_count, midbuf==out);
        if(midbuf != preout)
            out_count= resample(s, preout, out_count, midbuf, in_count);
    }

    if(preout != out && out_count){
        if(s->dither_method){
            int ch;
            int dither_count= FFMAX(out_count, 1<<16);
            av_assert0(preout != in);

            if((ret=realloc_audio(&s->dither, dither_count))<0)
                return ret;
            if(ret)
                for(ch=0; ch<s->dither.ch_count; ch++)
                    swri_get_dither(s, s->dither.ch[ch], s->dither.count, 12345678913579<<ch, s->out_sample_fmt, s->int_sample_fmt);
            av_assert0(s->dither.ch_count == preout->ch_count);

            if(s->dither_pos + out_count > s->dither.count)
                s->dither_pos = 0;

            for(ch=0; ch<preout->ch_count; ch++)
                s->mix_2_1_f(preout->ch[ch], preout->ch[ch], s->dither.ch[ch] + s->dither.bps * s->dither_pos, s->native_one, 0, 0, out_count);

            s->dither_pos += out_count;
        }
//FIXME packed doesnt need more than 1 chan here!
        swri_audio_convert(s->out_convert, out, preout, out_count);
    }
    return out_count;
}

int swr_convert(struct SwrContext *s, uint8_t *out_arg[SWR_CH_MAX], int out_count,
                                const uint8_t *in_arg [SWR_CH_MAX], int  in_count){
    AudioData * in= &s->in;
    AudioData *out= &s->out;

    if(s->drop_output > 0){
        int ret;
        AudioData tmp = s->out;
        uint8_t *tmp_arg[SWR_CH_MAX];
        tmp.count = 0;
        tmp.data  = NULL;
        if((ret=realloc_audio(&tmp, s->drop_output))<0)
            return ret;

        reversefill_audiodata(&tmp, tmp_arg);
        s->drop_output *= -1; //FIXME find a less hackish solution
        ret = swr_convert(s, tmp_arg, -s->drop_output, in_arg, in_count); //FIXME optimize but this is as good as never called so maybe it doesnt matter
        s->drop_output *= -1;
        if(ret>0)
            s->drop_output -= ret;

        av_freep(&tmp.data);
        if(s->drop_output || !out_arg)
            return 0;
        in_count = 0;
    }

    if(!in_arg){
        if(s->in_buffer_count){
            if (s->resample && !s->flushed) {
                AudioData *a= &s->in_buffer;
                int i, j, ret;
                if((ret=realloc_audio(a, s->in_buffer_index + 2*s->in_buffer_count)) < 0)
                    return ret;
                av_assert0(a->planar);
                for(i=0; i<a->ch_count; i++){
                    for(j=0; j<s->in_buffer_count; j++){
                        memcpy(a->ch[i] + (s->in_buffer_index+s->in_buffer_count+j  )*a->bps,
                            a->ch[i] + (s->in_buffer_index+s->in_buffer_count-j-1)*a->bps, a->bps);
                    }
                }
                s->in_buffer_count += (s->in_buffer_count+1)/2;
                s->resample_in_constraint = 0;
                s->flushed = 1;
            }
        }else{
            return 0;
        }
    }else
        fill_audiodata(in ,  (void*)in_arg);

    fill_audiodata(out, out_arg);

    if(s->resample){
        int ret = swr_convert_internal(s, out, out_count, in, in_count);
        if(ret>0 && !s->drop_output)
            s->outpts += ret * (int64_t)s->in_sample_rate;
        return ret;
    }else{
        AudioData tmp= *in;
        int ret2=0;
        int ret, size;
        size = FFMIN(out_count, s->in_buffer_count);
        if(size){
            buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
            ret= swr_convert_internal(s, out, size, &tmp, size);
            if(ret<0)
                return ret;
            ret2= ret;
            s->in_buffer_count -= ret;
            s->in_buffer_index += ret;
            buf_set(out, out, ret);
            out_count -= ret;
            if(!s->in_buffer_count)
                s->in_buffer_index = 0;
        }

        if(in_count){
            size= s->in_buffer_index + s->in_buffer_count + in_count - out_count;

            if(in_count > out_count) { //FIXME move after swr_convert_internal
                if(   size > s->in_buffer.count
                && s->in_buffer_count + in_count - out_count <= s->in_buffer_index){
                    buf_set(&tmp, &s->in_buffer, s->in_buffer_index);
                    copy(&s->in_buffer, &tmp, s->in_buffer_count);
                    s->in_buffer_index=0;
                }else
                    if((ret=realloc_audio(&s->in_buffer, size)) < 0)
                        return ret;
            }

            if(out_count){
                size = FFMIN(in_count, out_count);
                ret= swr_convert_internal(s, out, size, in, size);
                if(ret<0)
                    return ret;
                buf_set(in, in, ret);
                in_count -= ret;
                ret2 += ret;
            }
            if(in_count){
                buf_set(&tmp, &s->in_buffer, s->in_buffer_index + s->in_buffer_count);
                copy(&tmp, in, in_count);
                s->in_buffer_count += in_count;
            }
        }
        if(ret2>0 && !s->drop_output)
            s->outpts += ret2 * (int64_t)s->in_sample_rate;
        return ret2;
    }
}

int swr_drop_output(struct SwrContext *s, int count){
    s->drop_output += count;

    if(s->drop_output <= 0)
        return 0;

    av_log(s, AV_LOG_VERBOSE, "discarding %d audio samples\n", count);
    return swr_convert(s, NULL, s->drop_output, NULL, 0);
}

int swr_inject_silence(struct SwrContext *s, int count){
    int ret, i;
    AudioData silence = s->in;
    uint8_t *tmp_arg[SWR_CH_MAX];

    if(count <= 0)
        return 0;

    silence.count = 0;
    silence.data  = NULL;
    if((ret=realloc_audio(&silence, count))<0)
        return ret;

    if(silence.planar) for(i=0; i<silence.ch_count; i++) {
        memset(silence.ch[i], silence.bps==1 ? 0x80 : 0, count*silence.bps);
    } else
        memset(silence.ch[0], silence.bps==1 ? 0x80 : 0, count*silence.bps*silence.ch_count);

    reversefill_audiodata(&silence, tmp_arg);
    av_log(s, AV_LOG_VERBOSE, "adding %d audio samples of silence\n", count);
    ret = swr_convert(s, NULL, 0, (const uint8_t**)tmp_arg, count);
    av_freep(&silence.data);
    return ret;
}

int64_t swr_next_pts(struct SwrContext *s, int64_t pts){
    if(pts == INT64_MIN)
        return s->outpts;
    if(s->min_compensation >= FLT_MAX) {
        return (s->outpts = pts - swr_get_delay(s, s->in_sample_rate * (int64_t)s->out_sample_rate));
    } else {
        int64_t delta = pts - swr_get_delay(s, s->in_sample_rate * (int64_t)s->out_sample_rate) - s->outpts + s->drop_output*(int64_t)s->in_sample_rate;
        double fdelta = delta /(double)(s->in_sample_rate * (int64_t)s->out_sample_rate);

        if(fabs(fdelta) > s->min_compensation) {
            if(!s->outpts || fabs(fdelta) > s->min_hard_compensation){
                int ret;
                if(delta > 0) ret = swr_inject_silence(s,  delta / s->out_sample_rate);
                else          ret = swr_drop_output   (s, -delta / s-> in_sample_rate);
                if(ret<0){
                    av_log(s, AV_LOG_ERROR, "Failed to compensate for timestamp delta of %f\n", fdelta);
                }
            } else if(s->soft_compensation_duration && s->max_soft_compensation) {
                int duration = s->out_sample_rate * s->soft_compensation_duration;
                double max_soft_compensation = s->max_soft_compensation / (s->max_soft_compensation < 0 ? -s->in_sample_rate : 1);
                int comp = av_clipf(fdelta, -max_soft_compensation, max_soft_compensation) * duration ;
                av_log(s, AV_LOG_VERBOSE, "compensating audio timestamp drift:%f compensation:%d in:%d\n", fdelta, comp, duration);
                swr_set_compensation(s, comp, duration);
            }
        }

        return s->outpts;
    }
}
