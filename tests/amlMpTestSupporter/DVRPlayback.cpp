/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlMpPlayerDemo_DVRPlayback"
#include <utils/AmlMpLog.h>
#include "DVRPlayback.h"

static const char* mName = LOG_TAG;

namespace aml_mp {
DVRPlayback::DVRPlayback(const std::string& url, bool cryptoMode, Aml_MP_DemuxId demuxId, bool isTimeShift)
: mUrl(stripUrlIfNeeded(url))
, mCryptoMode(cryptoMode)
, mDemuxId(demuxId)
{
    MLOG();

    Aml_MP_DVRPlayerCreateParams createParams;
    memset(&createParams, 0, sizeof(createParams));
    createParams.basicParams.demuxId = mDemuxId;
    strncpy(createParams.basicParams.location, mUrl.c_str(), AML_MP_MAX_PATH_SIZE - 1);
    createParams.basicParams.blockSize = 188 * 1024;
    createParams.basicParams.isTimeShift = isTimeShift;
    createParams.basicParams.drmMode = AML_MP_INPUT_STREAM_NORMAL;

    MLOGI("mCryptoMode:%d", mCryptoMode);
    if (mCryptoMode) {
        if (initDVRDecryptPlayback(createParams.decryptParams) >= 0) {
            createParams.basicParams.blockSize = 256 * 1024;
            createParams.basicParams.drmMode = AML_MP_INPUT_STREAM_SECURE_MEMORY;
        }
    }

    int ret = Aml_MP_DVRPlayer_Create(&createParams, &mPlayer);
    if (ret < 0) {
        MLOGE("create dvr player failed!");
        return;
    }
    ret = Aml_MP_DVRPlayer_GetMpPlayerHandle(mPlayer, &mHandle);
    if (ret < 0) {
        MLOGE("get mp player failed!");
        return;
    }
}

DVRPlayback::~DVRPlayback()
{
    MLOG();

    if (mPlayer != AML_MP_INVALID_HANDLE) {
        Aml_MP_DVRPlayer_Destroy(mPlayer);
        mPlayer = AML_MP_INVALID_HANDLE;
    }

    MLOGI("dtor dvr playback end!");
}

#ifdef ANDROID
void DVRPlayback::setANativeWindow(const android::sp<ANativeWindow>& window)
{
    MLOG("setANativeWindow %p", window.get());
    Aml_MP_DVRPlayer_SetANativeWindow(mPlayer, window.get());
}
#endif

int DVRPlayback::setVideoWindow(int x, int y, int width, int height)
{
    int ret = Aml_MP_DVRPlayer_SetVideoWindow(mPlayer, x, y, width, height);
    return ret;
}

int DVRPlayback::setParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    int ret = Aml_MP_DVRPlayer_SetParameter(mPlayer, key, parameter);
    return ret;
}

void DVRPlayback::registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    mEventCallback = cb;
    mUserData = userData;
}

int DVRPlayback::setStreams()
{

    uint32_t segments;
    uint64_t* pSegmentIds = nullptr;
    int segmentIndex = 0;
    Aml_MP_DVRSegmentInfo segmentInfo;
    int ret = Aml_MP_DVRRecorder_GetSegmentList(mUrl.c_str(), &segments, &pSegmentIds);
    if (ret < 0) {
        MLOGE("getSegmentList for %s failed with %d", mUrl.c_str(), ret);
        return -1;
    }

    ret = Aml_MP_DVRRecorder_GetSegmentInfo(mUrl.c_str(), pSegmentIds[segmentIndex], &segmentInfo);
    if (ret < 0) {
        MLOGE("getSegmentInfo failed with %d", ret);
        goto exit;
    }

    Aml_MP_DVRStreamArray streams;
    memset(&streams, 0, sizeof(streams));
    for (int i = 0; i < segmentInfo.streams.nbStreams; ++i) {
        switch (segmentInfo.streams.streams[i].type) {
        case AML_MP_STREAM_TYPE_VIDEO:
            streams.streams[AML_MP_DVR_VIDEO_INDEX].type = AML_MP_STREAM_TYPE_VIDEO;
            streams.streams[AML_MP_DVR_VIDEO_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_VIDEO_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        case AML_MP_STREAM_TYPE_AUDIO:
            streams.streams[AML_MP_DVR_AUDIO_INDEX].type = AML_MP_STREAM_TYPE_AUDIO;
            streams.streams[AML_MP_DVR_AUDIO_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_AUDIO_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        case AML_MP_STREAM_TYPE_AD:
            streams.streams[AML_MP_DVR_AD_INDEX].type = AML_MP_STREAM_TYPE_AD;
            streams.streams[AML_MP_DVR_AD_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_AD_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        case AML_MP_STREAM_TYPE_SUBTITLE:
        case AML_MP_STREAM_TYPE_TELETEXT:
            streams.streams[AML_MP_DVR_SUBTITLE_INDEX].type = AML_MP_STREAM_TYPE_SUBTITLE;
            streams.streams[AML_MP_DVR_SUBTITLE_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_SUBTITLE_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        default:
            break;
        }
    }

    ret = Aml_MP_DVRPlayer_SetStreams(mPlayer, &streams);
    if (ret < 0) {
        MLOGE("dvr player set streams failed with %d", ret);
        goto exit;
    }

exit:
    if (pSegmentIds) {
        ::free(pSegmentIds);
        pSegmentIds = nullptr;
    }
    return 0;
}

int DVRPlayback::start(bool isSetStream)
{
#if 0
    bool useTif = false;
    Aml_MP_DVRPlayer_SetParameter(mPlayer, AML_MP_PLAYER_PARAMETER_USE_TIF, &useTif);
    Aml_MP_DVRPlayer_RegisterEventCallback(mPlayer, mEventCallback, mUserData);

    uint32_t segments;
    uint64_t* pSegmentIds = nullptr;
    int segmentIndex = 0;
    Aml_MP_DVRSegmentInfo segmentInfo;
    int ret = Aml_MP_DVRRecorder_GetSegmentList(mUrl.c_str(), &segments, &pSegmentIds);
    if (ret < 0) {
        MLOGE("getSegmentList for %s failed with %d", mUrl.c_str(), ret);
        return -1;
    }

    ret = Aml_MP_DVRRecorder_GetSegmentInfo(mUrl.c_str(), pSegmentIds[segmentIndex], &segmentInfo);
    if (ret < 0) {
        MLOGE("getSegmentInfo failed with %d", ret);
        goto exit;
    }

    Aml_MP_DVRStreamArray streams;
    memset(&streams, 0, sizeof(streams));
    for (int i = 0; i < segmentInfo.streams.nbStreams; ++i) {
        switch (segmentInfo.streams.streams[i].type) {
        case AML_MP_STREAM_TYPE_VIDEO:
            streams.streams[AML_MP_DVR_VIDEO_INDEX].type = AML_MP_STREAM_TYPE_VIDEO;
            streams.streams[AML_MP_DVR_VIDEO_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_VIDEO_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        case AML_MP_STREAM_TYPE_AUDIO:
            streams.streams[AML_MP_DVR_AUDIO_INDEX].type = AML_MP_STREAM_TYPE_AUDIO;
            streams.streams[AML_MP_DVR_AUDIO_INDEX].codecId = segmentInfo.streams.streams[i].codecId;
            streams.streams[AML_MP_DVR_AUDIO_INDEX].pid = segmentInfo.streams.streams[i].pid;
            break;

        default:
            break;
        }
    }

    ret = Aml_MP_DVRPlayer_SetStreams(mPlayer, &streams);
    if (ret < 0) {
        MLOGE("dvr player set streams failed with %d", ret);
        goto exit;
    }
#endif

    bool useTif = false;
    Aml_MP_DVRPlayer_SetParameter(mPlayer, AML_MP_PLAYER_PARAMETER_USE_TIF, &useTif);
    Aml_MP_DVRPlayer_RegisterEventCallback(mPlayer, mEventCallback, mUserData);

    if (isSetStream) {
        setStreams();
    }

    int ret = Aml_MP_DVRPlayer_Start(mPlayer, false);
    if (ret < 0) {
        MLOGE("dvr player start failed with %d", ret);
        //goto exit;
        return -1;
    }

#if 0
exit:
    if (pSegmentIds) {
        ::free(pSegmentIds);
        pSegmentIds = nullptr;
    }
#endif

    return 0;
}

int DVRPlayback::stop()
{
    MLOG();

    if (mPlayer != AML_MP_INVALID_HANDLE) {
        Aml_MP_DVRPlayer_Stop(mPlayer);
    }

    if (mCryptoMode) {
        uninitDVRDecryptPlayback();
    }

    MLOG();
    return 0;
}

std::string DVRPlayback::stripUrlIfNeeded(const std::string& url) const
{
    std::string result = url;

    auto suffix = result.rfind(".ts");
    if (suffix != result.npos) {
        auto hyphen = result.find_last_of('-', suffix);
        if (hyphen != result.npos) {
            result = result.substr(0, hyphen);
        }
    }
    result.erase(0, strlen("dvr://"));

    auto lsuffix = result.find_first_of('?');
    if (lsuffix != result.npos) {
        result = result.substr(0, lsuffix);
    }
    //for (;;) {
        //auto it = ++result.begin();
        //if (*it != '/') {
            //break;
        //}
        //result.erase(it);
    //}

    MLOGI("result str:%s", result.c_str());
    return result;
}

int DVRPlayback::initDVRDecryptPlayback(Aml_MP_DVRPlayerDecryptParams& decryptParams)
{
    MLOG();

    Aml_MP_CAS_Initialize();

    int ret = Aml_MP_CAS_OpenSession(&mCasSession, AML_MP_CAS_SERVICE_PVR_PLAY);
    if (ret < 0) {
        MLOGE("openSession failed! ret:%d", ret);
        return ret;
    }

    Aml_MP_CASDVRReplayParams params;
    params.dmxDev = mDemuxId;

    ret = Aml_MP_CAS_StartDVRReplay(mCasSession, &params);
    if (ret < 0) {
        MLOGE("start dvr replay failed with %d", ret);
        return ret;
    }

    decryptParams.cryptoData = this;
    decryptParams.cryptoFn = [](Aml_MP_CASCryptoParams* params, void* userdata) {
        DVRPlayback* playback = (DVRPlayback*)userdata;
        if (!playback->mDvrReplayInited) {
            playback->mDvrReplayInited = true;
            MLOGI("DVRReplay");
            int ret = Aml_MP_CAS_DVRReplay(playback->mCasSession, params);
            if (ret != AML_MP_OK) {
                MLOGE("CAS DVR replay failed, ret = %d", ret);
                return ret;
            }
            playback->mDvrReplayInited = true;
        }

        return Aml_MP_CAS_DVRDecrypt(playback->mCasSession, params);
    };

    uint8_t* secBuf = nullptr;
    uint32_t secBufSize;
    mSecMem = Aml_MP_CAS_CreateSecmem(mCasSession, AML_MP_CAS_SERVICE_PVR_PLAY, (void**)&secBuf, &secBufSize);
    if (mSecMem == nullptr) {
        MLOGE("create secMem failed");
        return -1;
    }

    decryptParams.secureBuffer = secBuf;
    decryptParams.secureBufferSize = secBufSize;
    return 0;
}

int DVRPlayback::uninitDVRDecryptPlayback()
{
    MLOG("mCasSession:%p", mCasSession);
    if (mCasSession == AML_MP_INVALID_HANDLE) {
        return 0;
    }

    Aml_MP_CAS_DestroySecmem(mCasSession, mSecMem);
    mSecMem = nullptr;

    Aml_MP_CAS_StopDVRReplay(mCasSession);

    Aml_MP_CAS_CloseSession(mCasSession);
    mCasSession = nullptr;

    mDvrReplayInited = false;
    return 0;
}

int DVRPlayback::getMpPlayerHandle(AML_MP_PLAYER* handle)
{
    int ret = -1;
    ret = Aml_MP_DVRPlayer_GetMpPlayerHandle(mPlayer, handle);
    return ret;
}

void DVRPlayback::signalQuit()
{
    MLOGI("signalQuit!");
}

///////////////////////////////////////////////////////////////////////////////
static struct TestModule::Command g_commandTable[] = {
    {
        "help", 0, "help",
        [](AML_MP_DVRPLAYER handle __unused, const std::vector<std::string>& args __unused) -> int {
            TestModule::printCommands(g_commandTable, true);
            return 0;
        }
    },

    {
        "pause", 0, "pause player",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_DVRPlayer_Pause(handle);
        }
    },

    {
        "resume", 0, "resume player",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_DVRPlayer_Resume(handle);
        }
    },

    {
        "seek", 0, "call seek",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_DVRPlayer_Seek(handle, 0);
            printf("call seek ret: %d\n", ret);
            return ret;
        }
    },

    {
        "start", 0, "call start",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_DVRPlayer_Start(handle, 0);
            printf("call start ret: %d\n", ret);
            return ret;
        }
    },

    {
        "stop", 0, "call stop",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_DVRPlayer_Stop(handle);
            printf("call stop ret: %d\n", ret);
            return ret;
        }
    },

    {
        "destroy", 0, "call destroy",
        [](AML_MP_DVRPLAYER handle, const std::vector<std::string>& args __unused) -> int {
            int ret = -1;
            ret = Aml_MP_DVRPlayer_Stop(handle);
            ret = Aml_MP_DVRPlayer_Destroy(handle);
            printf("call destroy ret: %d\n", ret);
            return ret;
        }
    },

    {nullptr, 0, nullptr, nullptr}
};

const TestModule::Command* DVRPlayback::getCommandTable() const
{
    return g_commandTable;
}

void* DVRPlayback::getCommandHandle() const
{
    //return nullptr;
    return mPlayer;
}



}
