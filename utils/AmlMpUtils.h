/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_MP_UTILS_H_
#define _AML_MP_UTILS_H_

#include "AmlMpLog.h"
#include <chrono>
#include <Aml_MP/Common.h>
#include <string>
#include <vector>
#ifdef HAVE_SUBTITLE
#include <SubtitleNativeAPI.h>
#endif
#include "AmlMpConfig.h"

#ifndef _LINUX_LIST_H
#define _LINUX_LIST_H
struct list_head {
    struct list_head *next, *prev;
};
#endif
#include <dvr_wrapper.h>
#include <dvb_utils.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef ANDROID
#include <amports/vformat.h>
#include <amports/aformat.h>
#include <utils/RefBase.h>
namespace android {
class NativeHandle;
}
#endif

namespace aml_mp {
#define AML_MP_UNUSED(x) (void)(x)

////////////////////////////////////////////////////////////////////////////////
#define AML_MP_LITERAL_TO_STRING_INTERNAL(x)    #x
#define AML_MP_LITERAL_TO_STRING(x) AML_MP_LITERAL_TO_STRING_INTERNAL(x)

#define AML_MP_CHECK(condition)                                     \
    AML_MP_LOG_ALWAYS_FATAL_IF(                                     \
            !(condition),                                           \
            "%s",                                                   \
            __FILE__ ":" AML_MP_LITERAL_TO_STRING(__LINE__)         \
            " CHECK(" #condition ") failed.")

#define AML_MP_CHECK_OP(x,y,suffix,op)                              \
    do {                                                            \
        const auto &a = x;                                          \
        const auto &b = y;                                          \
        if (!(a op b)) {                                            \
            std::string ___full =                                   \
                __FILE__ ":" AML_MP_LITERAL_TO_STRING(__LINE__)     \
                    " CHECK_" #suffix "( " #x "," #y ") failed: ";  \
            ___full.append(std::to_string(a));                      \
            ___full.append(" vs. ");                                \
            ___full.append(std::to_string(b));                      \
            AML_MP_LOG_ALWAYS_FATAL("%s", ___full.c_str());         \
        }                                                           \
    } while (false)

#define AML_MP_CHECK_EQ(x,y)   AML_MP_CHECK_OP(x,y,EQ,==)
#define AML_MP_CHECK_NE(x,y)   AML_MP_CHECK_OP(x,y,NE,!=)
#define AML_MP_CHECK_LE(x,y)   AML_MP_CHECK_OP(x,y,LE,<=)
#define AML_MP_CHECK_LT(x,y)   AML_MP_CHECK_OP(x,y,LT,<)
#define AML_MP_CHECK_GE(x,y)   AML_MP_CHECK_OP(x,y,GE,>=)
#define AML_MP_CHECK_GT(x,y)   AML_MP_CHECK_OP(x,y,GT,>)

void Aml_MP_PrintAssert(const char*fmt, ...);

#ifndef AML_MP_LOG_ALWAYS_FATAL_IF
#define AML_MP_LOG_ALWAYS_FATAL_IF(cond, fmt, ...) \
    ( (cond) \
    ? ((void)Aml_MP_PrintAssert(fmt, ## __VA_ARGS__)) \
    : (void)0 )
#endif

#ifndef AML_MP_LOG_ALWAYS_FATAL
#define AML_MP_LOG_ALWAYS_FATAL(fmt, ...) \
    ( ((void)Aml_MP_PrintAssert(fmt, ## __VA_ARGS__)) )
#endif

#define AML_MP_TRESPASS(...) \
    AML_MP_LOG_ALWAYS_FATAL( \
        __FILE__ ":" AML_MP_LITERAL_TO_STRING(__LINE__) " Should not be here. " __VA_ARGS__);


///////////////////////////////////////////////////////////////////////////////
#define RETURN_IF(error, cond)                                                       \
    do {                                                                             \
        if (cond) {                                                                  \
            MLOGE("%s:%d return %s <- %s", __FUNCTION__, __LINE__, #error, #cond);   \
            return error;                                                            \
        }                                                                            \
    } while (0)

#define RETURN_VOID_IF(cond)                                                         \
    do {                                                                             \
        if (cond) {                                                                  \
            MLOGE("%s:%d return <- %s", __FUNCTION__, __LINE__, #cond);              \
            return;                                                                  \
        }                                                                            \
    } while (0)

///////////////////////////////////////////////////////////////////////////////
#define AML_MP_TRACE(thresholdMs) FunctionLogger __flogger##__LINE__(mName, __FILE__, __FUNCTION__, __LINE__, thresholdMs, AmlMpConfig::instance().mLogDebug)

struct FunctionLogger
{
public:
    FunctionLogger(const char * tag, const char * file, const char * func, const int line, int threshold, bool verbose)
    : m_tag(tag), m_file(file), m_func(func), m_line(line), mThreshold(threshold), mVerbose(verbose)
    {
        (void)m_file;
        (void)m_line;

        mBeginTime = std::chrono::steady_clock::now();
        if (mVerbose) {
            ALOG(LOG_DEBUG, "Aml_MP", "%s %s() >> begin", m_tag, m_func);
        }
    }

    ~FunctionLogger()
    {
        auto endTime = std::chrono::steady_clock::now();
        int64_t duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - mBeginTime).count();

        if (mVerbose) {
            ALOG(LOG_DEBUG, "Aml_MP", "%s %s() << end %" PRId64 " ms", m_tag, m_func, duration);
        }

        if(duration >= mThreshold)
            ALOG(LOG_WARN, "Aml_MP", "%s %s() takes %" PRId64 " ms, Seems slow, check!", m_tag, m_func, duration);
    }

private:
    const char * m_tag;
    const char * m_file;
    const char * m_func;
    const int m_line;
    int mThreshold;
    bool mVerbose;
    std::chrono::steady_clock::time_point mBeginTime;
};

///////////////////////////////////////////////////////////////////////////////
const char* mpCodecId2Str(Aml_MP_CodecID codecId);
const char* mpStreamType2Str(Aml_MP_StreamType streamType);
const char* mpDemuxId2Str(Aml_MP_DemuxId demuxId);
const char* mpPlayerParameterKey2Str(Aml_MP_PlayerParameterKey playerParamKey);
const char* mpAVSyncSource2Str(Aml_MP_AVSyncSource syncSource);
const char* mpPlayerEventType2Str(Aml_MP_PlayerEventType eventType);
const char* mpPlayerWorkMode2Str(Aml_MP_PlayerWorkMode workmode);
const char* mpInputStreamType2Str(Aml_MP_InputStreamType inputStreamType);
const char* mpInputSourceType2Str(Aml_MP_InputSourceType inputSourceType);
const char* mpCASType2Str(Aml_MP_CASType casType);
const char* mpVideoDecodeMode2Str(Aml_MP_VideoDecodeMode decodeMode);
const char* mpCASServiceType2Str(Aml_MP_CASServiceType serviceType);
const char* mpVideoErrorRecoveryMode2Str(Aml_MP_VideoErrorRecoveryMode errorRecoveryMode);
const char* mpVideoAFDAspectMode2Str(Aml_MP_VideoAFDAspectMode afdAspectMode);

#ifdef ANDROID
vformat_t convertToVFormat(Aml_MP_CodecID videoCodec);
aformat_t convertToAForamt(Aml_MP_CodecID audioCodec);
#endif

am_tsplayer_video_codec convertToVideoCodec(Aml_MP_CodecID aml_MP_VideoCodec);
am_tsplayer_audio_codec convertToAudioCodec(Aml_MP_CodecID aml_MP_AudioCodec);
am_tsplayer_input_source_type convertToInputSourceType(Aml_MP_InputSourceType sourceType);
am_tsplayer_avsync_mode convertToAVSyncSourceType(Aml_MP_AVSyncSource avSyncSource);

Aml_MP_StreamType convertToMpStreamType(DVR_StreamType_t streamType);
DVR_StreamType_t convertToDVRStreamType(Aml_MP_StreamType streamType);

Aml_MP_CodecID convertToMpCodecId(DVR_VideoFormat_t fmt);
Aml_MP_CodecID convertToMpCodecId(DVR_AudioFormat_t fmt);
DVR_VideoFormat_t convertToDVRVideoFormat(Aml_MP_CodecID codecId);
DVR_AudioFormat_t convertToDVRAudioFormat(Aml_MP_CodecID codecId);

void convertToMpDVRStream(Aml_MP_DVRStream* mpDvrStream, DVR_StreamPid_t* dvrStream);
void convertToMpDVRStream(Aml_MP_DVRStream* mpDvrStream, DVR_StreamInfo_t* dvrStreamInfo);
void convertToMpDVRSourceInfo(Aml_MP_DVRSourceInfo* dest, DVR_WrapperInfo_t* source);

am_tsplayer_video_match_mode convertToTsPlayerVideoMatchMode(Aml_MP_VideoDisplayMode videoDisplayMode);
am_tsplayer_video_trick_mode convertToTsplayerVideoTrickMode(Aml_MP_VideoDecodeMode videoDecodeMode);
am_tsplayer_audio_out_mode convertToTsPlayerAudioOutMode(Aml_MP_AudioOutputMode audioOutputMode);
void convertToMpVideoInfo(Aml_MP_VideoInfo* mpVideoInfo, am_tsplayer_video_info* tsVideoInfo);
am_tsplayer_audio_stereo_mode convertToTsPlayerAudioStereoMode(Aml_MP_AudioBalance audioBalance);
void convertToMpPlayerEventAudioFormat(Aml_MP_PlayerEventAudioFormat* dest, am_tsplayer_audio_format_t* source);

DVB_DemuxSource_t convertToDVBDemuxSource(Aml_MP_DemuxSource source);
Aml_MP_DemuxSource convertToMpDemuxSource(DVB_DemuxSource_t source);
am_tsplayer_stream_type convertToTsplayerStreamType(Aml_MP_StreamType streamType);
am_tsplayer_media_time_type convertToTsplayerMediaTimeType(Aml_MP_StreamType streamType);
Aml_MP_StreamType convertToAmlMPStreamType(am_tsplayer_stream_type streamType);
am_tsplayer_input_buffer_type inputStreamTypeConvert(Aml_MP_InputStreamType streamType);

int convertToCodecRecoveryMode(Aml_MP_VideoErrorRecoveryMode errorRecoveryMode);
Aml_MP_VideoErrorRecoveryMode convertToAmlMPErrorRecoveryMode(int codecRecoveryMode);
void convertToTsPlayerAudioLanguage(am_tsplayer_audio_lang* tsPlayerAudioLang, Aml_MP_AudioLanguage* audioLanguage);

#ifdef HAVE_SUBTITLE
AmlTeletextCtrlParam convertToTeletextCtrlParam(AML_MP_TeletextCtrlParam* teletextCtrlParam);
AmlTeletextEvent convertToTeletextEvent(Aml_MP_TeletextEvent teletextEvent);
AML_MP_SubtitleDataType convertToMpSubtitleDataType(AmlSubDataType subDataType);
int convertToRegionId(char iso639lang[]);
#endif

bool isSupportMultiHwDemux();

struct NativeWindowHelper
{
    NativeWindowHelper();
    ~NativeWindowHelper();

    int setSidebandTunnelMode(ANativeWindow* nativeWindow);
    int setSidebandNonTunnelMode(ANativeWindow* nativeWindow, int* videoTunnelId);
    void clearTunnelId();

private:
#ifdef ANDROID
    ANativeWindow* mNativewindow = nullptr;
    android::sp<android::NativeHandle> mSidebandHandle;
    int mTunnelId = -1;
    int mMesonVtFd = -1;
#endif
    NativeWindowHelper(const NativeWindowHelper&) = delete;
    NativeWindowHelper& operator= (const NativeWindowHelper&) = delete;
};


int pushBlankBuffersToNativeWindow(ANativeWindow *nativeWindow /* nonnull */);

static inline pid_t gettid()
{
    return syscall(__NR_gettid);
}

const char* convertToMIMEString(Aml_MP_CodecID codecId);
Aml_MP_CodecID convertToMpCodecId(std::string mimeStr);

void split(const std::string& s, std::vector<std::string>& tokens, const std::string& delimiters = " ");
std::string trim(std::string& s, const std::string& chars = " \n");
int setTSNSourceToLocal();
int setTSNSourceToDemod();
void hexdump(const uint8_t* data, size_t size, std::string& result);
std::string getCommitInfo();
}

#endif
