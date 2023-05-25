/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpPlayerImpl"
#include "Aml_MP_PlayerImpl.h"
#include <utils/AmlMpLog.h>
#include <utils/AmlMpUtils.h>
#include <utils/AmlMpConfig.h>
#include <utils/AmlMpBuffer.h>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include "AmlPlayerBase.h"
#include "utils/Amlsysfsutils.h"

#ifdef ANDROID
#ifndef __ANDROID_VNDK__
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#endif
#include <cutils/properties.h>
#endif
#include <utils/AmlMpEventLooper.h>


namespace aml_mp {

#define TS_BUFFER_SIZE          (2 * 1024 * 1024)
#define TEMP_BUFFER_SIZE        (188 * 100)

#define START_ALL_PENDING       (1 << 0)
#define START_VIDEO_PENDING     (1 << 1)
#define START_AUDIO_PENDING     (1 << 2)
#define START_SUBTITLE_PENDING  (1 << 3)

#define FAST_PLAY_THRESHOLD     2.0f

#define DEFAULT_DEMUX_MEM_SEC_LEVEL     AML_MP_DEMUX_MEM_SEC_LEVEL2
#define DEFAULT_DEMUX_MEM_SEC_SIZE      (20<<20)//20M
#define PIP_DEMUX_MEM_SEC_SIZE          (36<<20)//36M
#define FCC_DEMUX_MEM_SEC_SIZE          (54<<20)//54M

///////////////////////////////////////////////////////////////////////////////
AmlMpPlayerImpl::AmlMpPlayerImpl(const Aml_MP_PlayerCreateParams* createParams)
: mInstanceId(AmlMpPlayerRoster::instance().registerPlayer(this))
, mCreateParams(*createParams)
{
    snprintf(mName, sizeof(mName), "%s_%d", LOG_TAG, mInstanceId);
    AmlMpConfig::instance().init();

#ifdef ANDROID
    int sdkVersion = ANDROID_PLATFORM_SDK_VERSION;
    const char* platform =
#ifndef __ANDROID_VNDK__
        "system";
#else
        "vendor";
#endif
    MLOG("sdk:%d, platform:%s, mTsPlayerNonTunnel: %d", sdkVersion, platform, AmlMpConfig::instance().mTsPlayerNonTunnel);
#endif

    MLOG("drmMode:%s, sourceType:%s", mpInputStreamType2Str(createParams->drmMode), mpInputSourceType2Str(createParams->sourceType));

    memset(&mVideoParams, 0, sizeof(mVideoParams));
    mVideoParams.pid = AML_MP_INVALID_PID;
    mVideoParams.videoCodec = AML_MP_CODEC_UNKNOWN;
    memset(&mAudioParams, 0, sizeof(mAudioParams));
    mAudioParams.pid = AML_MP_INVALID_PID;
    mAudioParams.audioCodec = AML_MP_CODEC_UNKNOWN;
    memset(&mSubtitleParams, 0, sizeof(mSubtitleParams));
    mSubtitleParams.pid = AML_MP_INVALID_PID;
    mSubtitleParams.subtitleCodec = AML_MP_CODEC_UNKNOWN;
    memset(&mADParams, 0, sizeof(mADParams));
    mADParams.pid = AML_MP_INVALID_PID;
    mADParams.audioCodec = AML_MP_CODEC_UNKNOWN;
    memset(&mIptvCasParams, 0, sizeof(mIptvCasParams));
    memset(&mTeletextCtrlParam, 0, sizeof(mTeletextCtrlParam));
    mTeletextCtrlParam.event = AML_MP_TT_EVENT_INVALID;

    memset(&mAudioLanguage, 0, sizeof(mAudioLanguage));

    mUserWaitingEcmMode = mWaitingEcmMode = (WaitingEcmMode)AmlMpConfig::instance().mWaitingEcmMode;
    MLOGI("mWaitingEcmMode:%d", mWaitingEcmMode);

    mTsBuffer.init(AmlMpConfig::instance().mWriteBufferSize * 1024 * 1024);

    mWriteBuffer = new AmlMpBuffer(TEMP_BUFFER_SIZE);
    mWriteBuffer->setRange(0, 0);
    mZorder = kZorderBase + mInstanceId;

    mPlayer = AmlPlayerBase::create(&mCreateParams, mInstanceId);

    //Set tsn source according to mpInputSource
    //1.dvrrecord and liveplay should set tsn_source to demod,
    //2.iptv should set tsn_source to local
    //3.tsd used when dvrplay, no need to consider tsn_source
    int ret = 0;
    if (mCreateParams.options & AML_MP_OPTION_DVR_PLAYBACK) {
        MLOGI("tsd used when dvrplay, no need to consider tsn_source");
    } else {
        if (createParams->sourceType == AML_MP_INPUT_SOURCE_TS_DEMOD) {
            ret = setTSNSourceToDemod();
        } else if (createParams->sourceType == AML_MP_INPUT_SOURCE_USBCAM){
            //Do nothing.
        } else {
            ret = setTSNSourceToLocal();
        }
    }
    if (ret)
        MLOGI("Error set tsn source, ret 0x%x\n", ret);

    // in CAS PIP case we need increase the sec buffer size
    increaseDmxSecMemSize();
}

AmlMpPlayerImpl::~AmlMpPlayerImpl()
{
    MLOG();

    if (mState != STATE_IDLE) {
        MLOG("Waring!! mState is not STATE_IDLE. Force to stop enter.");
        stop();
        MLOG("Force to stop exit.");
    }
    // recover demux sec mem size to default, this value same as DMC_MEM_DEFAULT_SIZE
    recoverDmxSecMemSize();

    /* Set Aspect Mode to NONE & Disable AFD when exit playback */
    if (mVideoAFDAspectMode >= 0) {
        setVideoAFDAspectMode_l(AML_MP_VIDEO_AFD_ASPECT_MODE_NONE);
    }

    AmlMpPlayerRoster::instance().unregisterPlayer(mInstanceId);
}

int AmlMpPlayerImpl::registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    MLOG();
    {
        std::unique_lock<std::mutex> _l(mLock);
        if (mState != STATE_IDLE) {
            MLOGW("can't registerEventCallback now!");
            return -1;
        }
    }

    std::unique_lock<std::mutex> _eventLock(mEventLock);
    mEventCb = cb;
    mUserData = userData;

    return 0;
}

int AmlMpPlayerImpl::setVideoParams(const Aml_MP_VideoParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);

    if (getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) != AML_MP_DECODING_STATE_STOPPED) {
        MLOGE("video started already!");
        return -1;
    }

    mVideoParams.pid = params->pid;
    mVideoParams.videoCodec = params->videoCodec;
    mVideoParams.width = params->width;
    mVideoParams.height = params->height;
    mVideoParams.frameRate = params->frameRate;
    memcpy(mVideoParams.extraData, params->extraData, sizeof(mVideoParams.extraData));
    mVideoParams.extraDataSize = params->extraDataSize;
    mVideoParams.secureLevel = params->secureLevel;

    MLOGI("setVideoParams vpid: 0x%x, fmt: %s", params->pid, mpCodecId2Str(params->videoCodec));

#if 1 // This commit commit config the secure level 2.
    if (mVideoParams.secureLevel == AML_MP_DEMUX_MEM_SEC_NONE && mCreateParams.drmMode != AML_MP_INPUT_STREAM_NORMAL) {
        mVideoParams.secureLevel = DEFAULT_DEMUX_MEM_SEC_LEVEL;
        MLOGI("change video secure level to %d", mVideoParams.secureLevel);
    }
#endif
    return 0;
}

int AmlMpPlayerImpl::setAudioParams(const Aml_MP_AudioParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);
    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) != AML_MP_DECODING_STATE_STOPPED) {
        MLOGE("audio started already!");
        return -1;
    }

    return setAudioParams_l(params);
}

int AmlMpPlayerImpl::setAudioParams_l(const Aml_MP_AudioParams* params)
{
    mAudioParams.pid = params->pid;
    mAudioParams.audioCodec = params->audioCodec;
    mAudioParams.nChannels = params->nChannels;
    mAudioParams.nSampleRate = params->nSampleRate;
    memcpy(mAudioParams.extraData, params->extraData, sizeof(mAudioParams.extraData));
    mAudioParams.extraDataSize = params->extraDataSize;
    mAudioParams.secureLevel = params->secureLevel;

    MLOGI("setAudioParams apid: 0x%x, fmt: %s", params->pid, mpCodecId2Str(params->audioCodec));

#if 1 // This commit config the secure level 2.
    if (mAudioParams.secureLevel == AML_MP_DEMUX_MEM_SEC_NONE && mCreateParams.drmMode != AML_MP_INPUT_STREAM_NORMAL) {
        mAudioParams.secureLevel = DEFAULT_DEMUX_MEM_SEC_LEVEL;
        MLOGI("change audio secure level to %d", mAudioParams.secureLevel);
    }
#endif
    return 0;
}

int AmlMpPlayerImpl::setSubtitleParams(const Aml_MP_SubtitleParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);

    if (getDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE) != AML_MP_DECODING_STATE_STOPPED) {
        MLOGE("subtitle started already!");
        return -1;
    }

    return setSubtitleParams_l(params);
}

int AmlMpPlayerImpl::setSubtitleParams_l(const Aml_MP_SubtitleParams* params)
{
    memcpy(&mSubtitleParams, params, sizeof(Aml_MP_SubtitleParams));

    MLOGI("setSubtitleParams spid: 0x%x, fmt:%s", params->pid, mpCodecId2Str(params->subtitleCodec));
    return 0;
}

int AmlMpPlayerImpl::setADParams(Aml_MP_AudioParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) != AML_MP_DECODING_STATE_STOPPED) {
        MLOGE("AD started already!");
        return -1;
    }

    mADParams.pid = params->pid;
    mADParams.audioCodec = params->audioCodec;
    mADParams.nChannels = params->nChannels;
    mADParams.nSampleRate = params->nSampleRate;
    memcpy(mADParams.extraData, params->extraData, sizeof(mADParams.extraData));
    mADParams.extraDataSize = params->extraDataSize;
    mADParams.secureLevel = params->secureLevel;

    MLOGI("setADParams apid: 0x%x, fmt:%s", params->pid, mpCodecId2Str(params->audioCodec));

#if 1 // This commit config the secure level 2.
    if (mADParams.secureLevel == AML_MP_DEMUX_MEM_SEC_NONE && mCreateParams.drmMode != AML_MP_INPUT_STREAM_NORMAL) {
        mADParams.secureLevel = DEFAULT_DEMUX_MEM_SEC_LEVEL;
        MLOGI("change ad secure level to %d", mADParams.secureLevel);
    }
#endif
    return 0;
}

int AmlMpPlayerImpl::setCasSession(AML_MP_CASSESSION casSession)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);
    MLOG();
    sptr<AmlCasBase> casBase = aml_handle_cast<AmlCasBase>(casSession);
    RETURN_IF(-1, casBase == nullptr);

    if (casBase->serviceType() < AML_MP_CAS_SERVICE_TYPE_IPTV) {
        MLOGE("do not support bind DVB caseSession!");
    } else if (casBase->serviceType() == AML_MP_CAS_SERVICE_VERIMATRIX_WEB) {
        MLOGW("verimatrix web casSession don't need bind!");
    } else if (casBase->serviceType() == AML_MP_CAS_SERVICE_NAGRA_WEB) {
        MLOGW("nagra web casSession don't need bind!");
    }

    mCasHandle = casBase;
    casBase->getEcmPids(mEcmPids);
    mIsStandaloneCas = true;

    return 0;
}

int AmlMpPlayerImpl::setIptvCASParams(Aml_MP_CASServiceType serviceType, const Aml_MP_IptvCASParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG();

    mCasServiceType = serviceType;
    mIptvCasParams = *params;

    return 0;
}

int AmlMpPlayerImpl::start()
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG();

    return start_l();
}

int AmlMpPlayerImpl::start_l()
{
    MLOGI("AmlMpPlayerImpl start_l\n");
    if (mState == STATE_IDLE) {
        if (prepare_l() < 0) {
            MLOGE("prepare failed!");
            return -1;
        }
    }

    if (mState == STATE_PREPARING) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_START_PENDING);

        return 0;
    }

    // dvr play case need set secure level to tsplayer
    if (mVideoParams.pid != AML_MP_INVALID_PID ||
        mCreateParams.options & AML_MP_OPTION_DVR_PLAYBACK) {
        mPlayer->setVideoParams(&mVideoParams);
    }

    if (mAudioParams.pid != AML_MP_INVALID_PID ||
        mCreateParams.options & AML_MP_OPTION_DVR_PLAYBACK) {
        mPlayer->setAudioParams(&mAudioParams);
        if (mAudioPresentationId >= 0) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID, &mAudioPresentationId);
        }
        if (mSPDIFStatus != -1) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_SPDIF_PROTECTION, &mSPDIFStatus);
        }
    }

    if (mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        mPlayer->setSubtitleParams(&mSubtitleParams);
    }

    int ret = 0;
    if (mVideoParams.pid != AML_MP_INVALID_PID ||
        mAudioParams.pid != AML_MP_INVALID_PID ||
        mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        ret = mPlayer->start();
        if (ret < 0) {
            MLOGE("%s failed!", __FUNCTION__);
            return ret;
        }
    }
    setState_l(STATE_RUNNING);

    //CHECK: assume start always be success if param exist
    if (mAudioParams.pid != AML_MP_INVALID_PID) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STARTED);
    }

    if (mVideoParams.pid != AML_MP_INVALID_PID) {
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_STARTED);
    }

    if (mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_STARTED);
    }

    startADDecoding_l();
    MLOGI("<<<< AmlMpPlayerImpl start_l\n");
    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::stop()
{
    std::unique_lock<std::mutex> lock(mLock);
    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);

    return stop_l(lock);
}

int AmlMpPlayerImpl::stop_l(std::unique_lock<std::mutex>& lock, bool clearCasSession)
{
    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        if (mPlayer) {
            mPlayer->stop();

            Aml_MP_AudioParams dummyAudioParam;
            memset(&dummyAudioParam, 0, sizeof(dummyAudioParam));
            dummyAudioParam.pid = AML_MP_INVALID_PID;
            dummyAudioParam.audioCodec = AML_MP_CODEC_UNKNOWN;
            mPlayer->setAudioParams(&dummyAudioParam);

            Aml_MP_AudioParams &dummyADParam = dummyAudioParam;
            mPlayer->setADParams(&dummyADParam, false);
        }
    }

    // ensure stream stopped if mState isn't running or paused
    setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STOPPED);
    setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_STOPPED);
    setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_STOPPED);
    setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_STOPPED);

    int ret = resetIfNeeded_l(lock, clearCasSession);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::pause()
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG();

    return pause_l();
}

int AmlMpPlayerImpl::pause_l() {
    if (mState != STATE_RUNNING) {
        return 0;
    }

    RETURN_IF(-1, mPlayer == nullptr);

    if (mPlayer->pause() < 0) {
        return -1;
    }

    setState_l(STATE_PAUSED);

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_STARTED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_PAUSED);
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_STARTED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_PAUSED);
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) == AML_MP_DECODING_STATE_STARTED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_PAUSED);
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE) == AML_MP_DECODING_STATE_STARTED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_PAUSED);
    }

    return 0;
}

int AmlMpPlayerImpl::resume()
{
    std::unique_lock<std::mutex> _l(mLock);

    return resume_l();
}

int AmlMpPlayerImpl::resume_l() {
    if (mState != STATE_PAUSED) {
        return 0;
    }

    RETURN_IF(-1, mPlayer == nullptr);

    int ret = 0;
    // video state
    if (getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) == AML_MP_DECODING_STATE_PAUSED) {
        ret += mPlayer->resumeVideoDecoding();
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_STARTED);
    }
    // audio state
    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_PAUSED) {
        ret += mPlayer->resumeAudioDecoding();
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STARTED);
    } else if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_STOPPED && mAudioStoppedInSwitching) {
        ret += startAudioDecoding_l();
    }
    // ad state
    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_PAUSED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_STARTED);
    }
    // subtitle state
    if (getDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE) == AML_MP_DECODING_STATE_PAUSED) {
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_STARTED);
    }

    if (ret < 0) {
        return -1;
    }

    setState_l(STATE_RUNNING);

    return 0;
}

int AmlMpPlayerImpl::flush()
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);

    if (mState != STATE_RUNNING && mState != STATE_PAUSED) {
        MLOGI("State is %s, can't call flush when not play", stateString(mState));
        return 0;
    }

    int ret;

    ret = mPlayer->flush();

    if (ret != AML_MP_ERROR_DEAD_OBJECT) {
        return ret;
    }

    ret = mPlayer->stop();
    if (ret < 0) {
        MLOGI("[%s, %d] stop play fail ret: %d", __FUNCTION__, __LINE__, ret);
    }
    if (getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) == AML_MP_DECODING_STATE_STARTED ||
        getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) == AML_MP_DECODING_STATE_PAUSED) {
        mPlayer->setVideoParams(&mVideoParams);
    }
    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_STARTED ||
        getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_PAUSED) {
        mPlayer->setAudioParams(&mAudioParams);
        if (mAudioPresentationId >= 0) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID, &mAudioPresentationId);
        }
        if (mSPDIFStatus != -1) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_SPDIF_PROTECTION, &mSPDIFStatus);
        }
    }
    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_STARTED ||
        getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_PAUSED) {
        mPlayer->setADParams(&mADParams, true);
    }

    if (tryWaitEcm_l()) {
        setState_l(STATE_PREPARING);
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_START_PENDING);
        setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_START_PENDING);
        return ret;
    }

    ret = mPlayer->start();
    if (ret < 0) {
        MLOGI("[%s, %d] start play fail ret: %d", __FUNCTION__, __LINE__, ret);
    }

    if (mState == STATE_PAUSED) {
        ret = mPlayer->pause();
    }

    return ret;
}

// Smooth playback between 0 and 2x speed (0 < speed <= 2), audio will be resample with play speed
// I-frmae playback when speed > 2 or speed < 0, audio will not be processed when I-frmae playback
int AmlMpPlayerImpl::setPlaybackRate(float rate)
{
    std::unique_lock<std::mutex> lock(mLock);
    MLOG("rate:%f", rate);

    int ret = 0;
    Aml_MP_VideoDecodeMode newDecodeMode;
    bool decodeModeChanged = false;

    mPlaybackRate = rate;

    //newDecodeMode = (mPlaybackRate > FAST_PLAY_THRESHOLD)?AML_MP_VIDEO_DECODE_MODE_IONLY:AML_MP_VIDEO_DECODE_MODE_NONE;
    if (mPlaybackRate > FAST_PLAY_THRESHOLD || mPlaybackRate < 0) {
        newDecodeMode = (mCreateParams.sourceType == AML_MP_INPUT_SOURCE_ES_MEMORY) ?
                AML_MP_VIDEO_DECODE_MODE_IONLY : AML_MP_VIDEO_DECODE_MODE_PAUSE_NEXT;
    } else {
        newDecodeMode = AML_MP_VIDEO_DECODE_MODE_NONE;
    }
    decodeModeChanged = (newDecodeMode != mVideoDecodeMode);

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);

        if (mPlaybackRate == 0.0f) {
            ret = pause_l();
            return ret;
        }

        if (mState == STATE_PAUSED) {
            resume_l();
        }

        if (decodeModeChanged) {
            switchDecodeMode_l(newDecodeMode, lock);
        }

        ret = mPlayer->setPlaybackRate(rate);
    }

    return ret;
}

int AmlMpPlayerImpl::getPlaybackRate(float* rate) {
    std::unique_lock<std::mutex> _l(mLock);
    if (rate == nullptr) {
        return AML_MP_ERROR;
    }

    int ret = mPlayer->getPlaybackRate(rate);
    if (ret != AML_MP_OK) {
        *rate = mPlaybackRate;
    }
    //MLOG("rate: %f, mPlaybackRate: %f", *rate, mPlaybackRate);

    return ret;
}

int AmlMpPlayerImpl::switchAudioTrack(const Aml_MP_AudioParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG("new apid: 0x%x, fmt:%s", params->pid, mpCodecId2Str(params->audioCodec));
    int ret = -1;

    setAudioParams_l(params);

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        ret = mPlayer->switchAudioTrack(params);

        if (ret != AML_MP_ERROR_DEAD_OBJECT) {
            return ret;
        }

        ret = mPlayer->stopAudioDecoding();
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STOPPED);
        if (mState == STATE_RUNNING) {
            ret += startAudioDecoding_l();
        } else {
            mAudioStoppedInSwitching = true;
        }
    }

    return ret;
}

int AmlMpPlayerImpl::switchSubtitleTrack(const Aml_MP_SubtitleParams* params)
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG("new spid: 0x%x, fmt:%s", params->pid, mpCodecId2Str(params->subtitleCodec));
    int ret = 0;

    setSubtitleParams_l(params);

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        ret = mPlayer->switchSubtitleTrack(params);
    }

    return ret;
}

int AmlMpPlayerImpl::writeData(const uint8_t* buffer, size_t size)
{
    std::unique_lock<std::mutex> lock(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    int written = 0;

    bool needBuffering = false;
    if (mCasHandle && mCreateParams.drmMode == AML_MP_INPUT_STREAM_ENCRYPTED && mWaitingEcmMode == kWaitingEcmSynchronous && !mFirstEcmWritten) {
        size_t ecmOffset = size;
        size_t ecmSize = 188;
        ecmOffset = findEcmPacket(buffer, size, mEcmPids, &ecmSize);
        if (ecmSize > 0) {
            mCasHandle->processEcm(false, 0, buffer + ecmOffset, ecmSize);
            mFirstEcmWritten = true;
            MLOGI("first ECM written, offset:%zu", mTsBuffer.size() + ecmOffset);
        } else {
            needBuffering = true;
        }
    }

    if (mState == STATE_PREPARING || needBuffering) {
        //is waiting for start_delay, writeData into mTsBuffer
        if (mTsBuffer.space() < size) {
            MLOGW("mTsBuffer full!");
            return -1;
        }
        written = mTsBuffer.put(buffer, size); //TODO: check buffer overflow
        if (mParser != nullptr) {
            written = mParser->writeData(buffer, size);
        }
    } else {
        //already start, need move data from mTsBuffer to player
        if (!mTsBuffer.empty() || mWriteBuffer->size() != 0) {
            if (drainDataFromBuffer_l(lock) != 0) {
                return -1;
            }
        }

        written = doWriteData_l(buffer, size, lock);
    }

    if (written == 0) {
        written = -1;
    }

    if (written > 0) {
        statisticWriteDataRate_l(written);
    }

    return written;
}

int AmlMpPlayerImpl::drainDataFromBuffer_l(std::unique_lock<std::mutex>& lock)
{
    int written = 0;
    int retry = 0;
    do {
        if (mWriteBuffer->size() == 0) {
            size_t readSize = mTsBuffer.get(mWriteBuffer->base(), mWriteBuffer->capacity());
            mWriteBuffer->setRange(0, readSize);
        }

        written = doWriteData_l(mWriteBuffer->data(), mWriteBuffer->size(), lock);

        if (written > 0) {
            mWriteBuffer->setRange(mWriteBuffer->offset()+written, mWriteBuffer->size()-written);
        } else {
            if (retry >= 4) {
                break;
            }
            retry++;
            usleep(50 * 1000);
        }
    } while (!mTsBuffer.empty() || mWriteBuffer->size() != 0);

    if (mTsBuffer.empty() && mWriteBuffer->size() == 0) {
        MLOGI("writeData from buffer done");
        return 0;
    }

    return -EAGAIN;
}

int AmlMpPlayerImpl::doWriteData_l(const uint8_t* buffer, size_t size, std::unique_lock<std::mutex>& lock)
{
    int written = 0;
    if (mCreateParams.drmMode == AML_MP_INPUT_STREAM_ENCRYPTED) {
        if (mCasHandle == nullptr || mWaitingEcmMode == kWaitingEcmASynchronous) {
            lock.unlock();
            written = mPlayer->writeData(buffer, size);
            lock.lock();
        } else {
            size_t totalSize = size;
            size_t ecmOffset = size;
            size_t ecmSize = 188;
            int ecmCount = 0;

            while (size) {
                ecmOffset = findEcmPacket(buffer, size, mEcmPids, &ecmSize);
                ecmCount += ecmSize != 0;

                size_t partialSize = ecmOffset;
                int ret = 0;
                int retryCount = 0;
                while (partialSize) {
                    lock.unlock();
                    ret = mPlayer->writeData(buffer, partialSize);
                    lock.lock();
                    if (ret <= 0) {
                        if (written == 0) {
                            goto exit;
                        }
                        usleep(50 * 1000);

                        ++retryCount;
                        if (retryCount%40 == 0) {
                            MLOGI("writeData %d/%zu(%zu), ecmOffset:%zu(%d), return:%d", written, totalSize, size, ecmOffset, ecmCount, ret);
                        }
                    } else {
                        buffer += ret;
                        partialSize -= ret;
                        written += ret;
                        size -= ret;
                    }
                };
                if (ecmSize > 0) {
                    mCasHandle->processEcm(false, 0, buffer, ecmSize);
                    buffer += ecmSize;
                    written += ecmSize;
                    size -= ecmSize;
                }
            }
        }
    } else if (mCreateParams.drmMode == AML_MP_INPUT_STREAM_SECURE_MEMORY) {
        lock.unlock();
        written = mPlayer->writeData(buffer, size);
        lock.lock();
    } else {
        //normal stream
        lock.unlock();
        written = mPlayer->writeData(buffer, size);
        lock.lock();
    }

exit:
    return written;
}

int AmlMpPlayerImpl::writeEsData(Aml_MP_StreamType type, const uint8_t* buffer, size_t size, int64_t pts)
{
    MLOGD("[%s_%s] buffer:%p, size:%zu, pts:%#" PRIx64 "(%.3fs)", __FUNCTION__, mpStreamType2Str(type), buffer, size, pts, pts/9e4);
    //audio isn`t support non-blocking mode, must different threads to write av es
    //std::unique_lock<std::mutex> _l(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->writeEsData(type, buffer, size, pts);
}

int AmlMpPlayerImpl::getCurrentPts(Aml_MP_StreamType streamType, int64_t* pts)
{
    std::unique_lock<std::mutex> _l(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->getCurrentPts(streamType, pts);
}

int AmlMpPlayerImpl::getFirstPts(Aml_MP_StreamType streamType, int64_t* pts)
{
    std::unique_lock<std::mutex> _l(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->getFirstPts(streamType, pts);
}

int AmlMpPlayerImpl::getBufferStat(Aml_MP_BufferStat* bufferStat)
{
    std::unique_lock<std::mutex> _l(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->getBufferStat(bufferStat);
}

int AmlMpPlayerImpl::setANativeWindow(ANativeWindow* nativeWindow)
{
    std::unique_lock<std::mutex> _l(mLock);

    #ifdef ANDROID
    MLOGI("setAnativeWindow: %p, mNativewindow: %p", nativeWindow, mNativeWindow.get());
    mNativeWindow = nativeWindow;

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        mPlayer->setANativeWindow(mNativeWindow.get());
        setSidebandIfNeeded_l();
    }
    #endif
    return 0;
}

int AmlMpPlayerImpl::setSidebandIfNeeded_l()
{
#ifdef ANDROID
    if (mNativeWindow == nullptr) {
        return 0;
    }

    if (AmlMpConfig::instance().mTsPlayerNonTunnel || mCreateParams.sourceType == AML_MP_INPUT_SOURCE_ES_MEMORY) {
        if (AmlMpConfig::instance().mUseVideoTunnel == 1) {
            if (mNativeWindowHelper.setSidebandNonTunnelMode(mNativeWindow.get(), &mVideoTunnelId) == 0) {
                MLOGI("allocated video tunnel id:%d", mVideoTunnelId);
            }
        }
    } else {
        mNativeWindowHelper.setSidebandTunnelMode(mNativeWindow.get());
    }
#endif

    return 0;
}

int AmlMpPlayerImpl::setVideoWindow(int x, int y, int width, int height)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);

    if (width < 0 || height < 0) {
        MLOGI("Invalid windowsize: %dx%d, return fail", width, height);
        return -1;
    }
#ifdef ANDROID
#ifndef __ANDROID_VNDK__
    if (mNativeWindow == nullptr) {
        MLOGI("Nativewindow is null, create it");
        mComposerClient = new android::SurfaceComposerClient;
        mComposerClient->initCheck();

        mSurfaceControl = mComposerClient->createSurface(android::String8("AmlMpPlayer"), width, height, android::PIXEL_FORMAT_RGB_565, 0);
        if (mSurfaceControl->isValid()) {
            mSurface = mSurfaceControl->getSurface();
            android::SurfaceComposerClient::Transaction()
                .setFlags(mSurfaceControl, android::layer_state_t::eLayerOpaque, android::layer_state_t::eLayerOpaque)
                .show(mSurfaceControl)
                .apply();
        }
        mNativeWindow = mSurface;
    }

    if (mSurfaceControl != nullptr) {
        MLOGI("Set video window size: x %d, y %d, width: %d, height: %d", x, y, width, height);
        auto transcation = android::SurfaceComposerClient::Transaction();
        if (x >= 0 && y >= 0) {
            transcation.setPosition(mSurfaceControl, x, y);
        }

        transcation.setSize(mSurfaceControl, width, height);
#if ANDROID_PLATFORM_SDK_VERSION >= 31
        transcation.setCrop(mSurfaceControl, android::Rect(width, height));
#elif ANDROID_PLATFORM_SDK_VERSION >= 29
        transcation.setCrop_legacy(mSurfaceControl, android::Rect(width, height));
#endif
        transcation.setLayer(mSurfaceControl, mZorder);
        transcation.apply();

        // setVideoWindow is used for create surface now, don't need to call the underlying implementation.
        return 0;
    }
#endif
#endif

    mVideoWindow = {x, y, width, height};
    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        if (mVideoWindow.width >= 0 && mVideoWindow.height >= 0) {
            mPlayer->setVideoWindow(x, y, width, height);
        }
    }

    return 0;
}

int AmlMpPlayerImpl::setVolume(float volume)
{
    std::unique_lock<std::mutex> _l(mLock);

    int ret = 0;
    if (volume < 0) {
        MLOGI("volume is %f, set to 0.0", volume);
        volume = 0.0;
    }
    mVolume = volume;

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        ret = mPlayer->setVolume(volume);
    }

    return ret;
}

int AmlMpPlayerImpl::getVolume(float* volume)
{
    std::unique_lock<std::mutex> _l(mLock);

    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->getVolume(volume);
}

int AmlMpPlayerImpl::showVideo()
{
    MLOG();
    std::unique_lock<std::mutex> _l(mLock);

    RETURN_IF(-1, mPlayer == nullptr);

    mVideoShowState = true;
    return mPlayer->showVideo();
}

int AmlMpPlayerImpl::hideVideo()
{
    MLOG();
    std::unique_lock<std::mutex> _l(mLock);

    RETURN_IF(-1, mPlayer == nullptr);

    mVideoShowState = false;
    return mPlayer->hideVideo();
}

int AmlMpPlayerImpl::showSubtitle()
{
    MLOG();
    std::unique_lock<std::mutex> _l(mLock);

    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->showSubtitle();
}

int AmlMpPlayerImpl::hideSubtitle()
{
    MLOG();
    std::unique_lock<std::mutex> _l(mLock);

    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->hideSubtitle();
}

int AmlMpPlayerImpl::setParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> lock(mLock);

    return setParameter_l(key, parameter, lock);
}

int AmlMpPlayerImpl::setParameter_l(Aml_MP_PlayerParameterKey key, void* parameter, std::unique_lock<std::mutex>& lock) {
    int ret = 0;

    switch (key) {
    case AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE:
        RETURN_IF(-1, parameter == nullptr);
        mVideoDisplayMode = *(Aml_MP_VideoDisplayMode*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_BLACK_OUT:
        RETURN_IF(-1, parameter == nullptr);
        mBlackOut = *(bool*)parameter?1:0;
        break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_DECODE_MODE:
    {
        RETURN_IF(-1, parameter == nullptr);
        Aml_MP_VideoDecodeMode decodeMode = *(Aml_MP_VideoDecodeMode*)parameter;
        Aml_MP_AVSyncSource originalSyncSource = mSyncSource;
        if (AML_MP_VIDEO_DECODE_MODE_NONE != decodeMode) {
            mSyncSource = AML_MP_AVSYNC_SOURCE_NOSYNC;
        }
        switchDecodeMode_l(decodeMode, lock);
        mSyncSource = originalSyncSource;
        break;
    }

    case AML_MP_PLAYER_PARAMETER_VIDEO_PTS_OFFSET:
        RETURN_IF(-1, parameter == nullptr);
        mVideoPtsOffset = *(int*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_MODE:
        RETURN_IF(-1, parameter == nullptr);
        mAudioOutputMode = *(Aml_MP_AudioOutputMode*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_DEVICE:
        RETURN_IF(-1, parameter == nullptr);
        mAudioOutputDevice = *(Aml_MP_AudioOutputDevice*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_PTS_OFFSET:
        RETURN_IF(-1, parameter == nullptr);
        mAudioPtsOffset = *(int*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_BALANCE:
        RETURN_IF(-1, parameter == nullptr);
        mAudioBalance = *(Aml_MP_AudioBalance*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_MUTE:
        RETURN_IF(-1, parameter == nullptr);
        mAudioMute = *(bool*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_NETWORK_JITTER:
        RETURN_IF(-1, parameter == nullptr);
        mNetworkJitter = *(int*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_AD_STATE:
    {
        RETURN_IF(-1, parameter == nullptr);
        int ADState = *(int*)parameter;
        if (ADState) {
            startADDecoding_l();
        } else {
            stopADDecoding_l(lock);
        }
        return 0;
    }

    case AML_MP_PLAYER_PARAMETER_AD_MIX_LEVEL:
        RETURN_IF(-1, parameter == nullptr);
        mADMixLevel = *(Aml_MP_ADVolume*)parameter;
        break;

    case AML_MP_PLAYER_PARAMETER_WORK_MODE:
    {
        mWorkMode = *(Aml_MP_PlayerWorkMode*)parameter;
        MLOGI("Set work mode: %s", mpPlayerWorkMode2Str(mWorkMode));
    }
    break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_WINDOW_ZORDER:
    {
        mZorder = *(int*)parameter;
        MLOGI("Set zorder: %d", mZorder);
#ifdef ANDROID
#ifndef __ANDROID_VNDK__
        if (mSurfaceControl != nullptr) {
            auto transcation = android::SurfaceComposerClient::Transaction();

            transcation.setLayer(mSurfaceControl, mZorder);

            transcation.apply();
        }
#endif
#endif
    }
    break;

    case AML_MP_PLAYER_PARAMETER_CREATE_PARAMS:
    {
        RETURN_IF(-1, parameter == nullptr);
        mCreateParams = *(Aml_MP_PlayerCreateParams *)parameter;
        MLOGI("Set mCreateParams drmmode:%s", mpInputStreamType2Str(mCreateParams.drmMode));
        return 0;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID:
    {
        RETURN_IF(-1, parameter == nullptr);
        mVideoTunnelId = *(int*)parameter;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_SURFACE_HANDLE:
    {
        RETURN_IF(-1, parameter == nullptr);
        MLOGI("set surface handle: %p", parameter);
        mSurfaceHandle = parameter;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID:
    {
        RETURN_IF(-1, parameter == nullptr);
        mAudioPresentationId = *(int*)parameter;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_USE_TIF:
    {
        RETURN_IF(-1, parameter == nullptr);
        mUseTif = *(bool*)parameter;
    }
    break;

    case  AML_MP_PLAYER_PARAMETER_TELETEXT_CONTROL:
    {
        RETURN_IF(-1, parameter == nullptr);
        mTeletextCtrlParam = *(AML_MP_TeletextCtrlParam*)parameter;
        MLOGW("ttx key: %s", mpPlayerParameterKey2Str(key));
    }
    break;

    case AML_MP_PLAYER_PARAMETER_SPDIF_PROTECTION:
    {
        RETURN_IF(-1, parameter == nullptr);
        mSPDIFStatus = *(int*)parameter;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_CROP:
    {
        Aml_MP_Rect videoCrop = *(Aml_MP_Rect*)parameter;
        mVideoCrop = videoCrop;
        MLOGI("set source crop:[%d, %d, %d, %d]", mVideoCrop.left, mVideoCrop.top, mVideoCrop.right, mVideoCrop.bottom);
    }
    break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_ERROR_RECOVERY_MODE:
    {
        mVideoErrorRecoveryMode = *(Aml_MP_VideoErrorRecoveryMode*)parameter;
        MLOGI("Set error recovery mode: %s", mpVideoErrorRecoveryMode2Str(mVideoErrorRecoveryMode));
    }
    break;

    case AML_MP_PLAYER_PARAMETER_VIDEO_AFD_ASPECT_MODE:
    {
        RETURN_IF(-1, parameter == nullptr);
        int ret = 0;
        mVideoAFDAspectMode = *(Aml_MP_VideoAFDAspectMode*)parameter;
        MLOGI("Set video AFD aspect mode: %s", mpVideoAFDAspectMode2Str(mVideoAFDAspectMode));
        if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
            ret = setVideoAFDAspectMode_l(mVideoAFDAspectMode);
        }

        return ret;
    }
    break;

    case AML_MP_PLAYER_PARAMETER_AUDIO_LANGUAGE:
    {
        RETURN_IF(-1, parameter == nullptr);
        mAudioLanguage = *(Aml_MP_AudioLanguage*)parameter;
        MLOGI("set audio language:[%#x, %#x]", mAudioLanguage.firstLanguage, mAudioLanguage.secondLanguage);
        break;
    }

    default:
        MLOGW("unhandled key: %s", mpPlayerParameterKey2Str(key));
        return ret;
    }

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        ret = mPlayer->setParameter(key, parameter);
    }

    return ret;
}

int AmlMpPlayerImpl::getParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    AML_MP_TRACE(10);
    bool locked = false;

    //allow call getParameter in event callback.
    pid_t eventCbTid = mEventCbTid.load(std::memory_order_relaxed);
    if (eventCbTid == -1 || eventCbTid != gettid()) {
        mLock.lock();
        locked = true;
    } else {
        MLOGW("don't acquire lock for inner event cb thread!");
    }

    int ret = -1;
    if (mPlayer != nullptr) {
        ret = mPlayer->getParameter(key, parameter);
    }

    if (ret != AML_MP_OK) {
        switch (key) {
        case AML_MP_PLAYER_PARAMETER_VIDEO_SHOW_STATE:
        {
            *static_cast<bool*>(parameter) = mVideoShowState;
            ret = AML_MP_OK;
            break;
        }
        case AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID:
        {
            *static_cast<int*>(parameter) = mVideoTunnelId;
            ret = AML_MP_OK;
            break;
        }

        case AML_MP_PLAYER_PARAMETER_VIDEO_AFD_ASPECT_MODE:
        {
            *static_cast<Aml_MP_VideoAFDAspectMode*>(parameter) = mVideoAFDAspectMode;
            ret = AML_MP_OK;
            break;
        }

        default:
            break;
        }

    }

    if (locked) {
        mLock.unlock();
    }

    return ret;
}

int AmlMpPlayerImpl::setAVSyncSource(Aml_MP_AVSyncSource syncSource)
{
    MLOGI("%s ! mSyncSource %d", __FUNCTION__, syncSource);
    std::unique_lock<std::mutex> _l(mLock);

    mSyncSource = syncSource;

    return 0;
}

int AmlMpPlayerImpl::setPcrPid(int pid)
{
    MLOGI("%s ! setPcrPid %d", __FUNCTION__, pid);
    std::unique_lock<std::mutex> _l(mLock);

    mPcrPid = pid;

    return 0;
}

int AmlMpPlayerImpl::startVideoDecoding()
{
    std::unique_lock<std::mutex> _l(mLock);

    return startVideoDecoding_l();
}

int AmlMpPlayerImpl::startVideoDecoding_l()
{
    MLOG();

    if (mState == STATE_IDLE) {
        if (prepare_l() < 0) {
            MLOGE("prepare failed!");
            return -1;
        }
    }

    if (mState == STATE_PREPARING) {
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_START_PENDING);
        return 0;
    }

    if (mVideoParams.pid != AML_MP_INVALID_PID) {
        mPlayer->setVideoParams(&mVideoParams);
    }

    int ret = mPlayer->startVideoDecoding();
    if (ret < 0) {
        MLOGE("%s failed!", __FUNCTION__);
        return -1;
    }

    setState_l(STATE_RUNNING);
    if (mVideoParams.pid != AML_MP_INVALID_PID) {
        setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_STARTED);
    }

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::stopVideoDecoding()
{
    std::unique_lock<std::mutex> lock(mLock);

    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);
    MLOGI("stopVideoDecoding\n");
    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        if (mPlayer) {
            mPlayer->stopVideoDecoding();
        }
    }

    // ensure stream stopped if mState isn't running or paused
    setDecodingState_l(AML_MP_STREAM_TYPE_VIDEO, AML_MP_DECODING_STATE_STOPPED);

    int ret = resetIfNeeded_l(lock);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::startAudioDecoding()
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOGI("startAudioDecoding\n");
    return startAudioDecoding_l();
}

int AmlMpPlayerImpl::startAudioDecoding_l()
{
    MLOG();
    int ret;

    if (mState == STATE_IDLE) {
        if (prepare_l() < 0) {
            MLOGE("prepare failed!");
            return -1;
        }
    }

    if (mState == STATE_PREPARING) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_START_PENDING);
        return 0;
    }

    if (mAudioParams.pid == AML_MP_INVALID_PID) {
        return 0;
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_STARTED) {
        resetADCodec_l(false);
    }

    mPlayer->setAudioParams(&mAudioParams);
    if (mAudioPresentationId >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID, &mAudioPresentationId);
    }
    if (mSPDIFStatus != -1) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_SPDIF_PROTECTION, &mSPDIFStatus);
    }

    ret = mPlayer->startAudioDecoding();
    if (ret < 0) {
        MLOGE("%s failed!", __FUNCTION__);
        return -1;
    }

    setState_l(STATE_RUNNING);

    setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STARTED);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::stopAudioDecoding()
{
    std::unique_lock<std::mutex> lock(mLock);

    return stopAudioDecoding_l(lock);
}

int AmlMpPlayerImpl::stopAudioDecoding_l(std::unique_lock<std::mutex>& lock)
{
    MLOG();
    int ret;
    RETURN_IF(-1, mPlayer == nullptr);

    mAudioPresentationId = -1;
    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        if (mPlayer) {
            mPlayer->stopAudioDecoding();

            Aml_MP_AudioParams dummyAudioParam;
            memset(&dummyAudioParam, 0, sizeof(dummyAudioParam));
            dummyAudioParam.pid = AML_MP_INVALID_PID;
            dummyAudioParam.audioCodec = AML_MP_CODEC_UNKNOWN;
            mPlayer->setAudioParams(&dummyAudioParam);
        }

        if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_STARTED) {
            resetADCodec_l(true);
        }
    }

    // ensure stream stopped if mState isn't running or paused
    setDecodingState_l(AML_MP_STREAM_TYPE_AUDIO, AML_MP_DECODING_STATE_STOPPED);
    mAudioStoppedInSwitching = false;

    ret = resetIfNeeded_l(lock);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::startADDecoding()
{
    std::unique_lock<std::mutex> _l(mLock);

    return startADDecoding_l();
}

int AmlMpPlayerImpl::startADDecoding_l()
{
    MLOG();
    int ret;

    if (mState == STATE_IDLE) {
        if (prepare_l() < 0) {
            MLOGE("prepare failed!");
            return -1;
        }
    }

    if (mState == STATE_PREPARING) {
        setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_START_PENDING);
        return 0;
    }

    if (mADParams.pid == AML_MP_INVALID_PID) {
        return 0;
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_STARTED) {
        resetAudioCodec_l(false);
    }

    mPlayer->setADParams(&mADParams, true);

    ret = mPlayer->startADDecoding();
    if (ret < 0) {
        MLOGE("%s failed!", __FUNCTION__);
        return -1;
    }

    setState_l(STATE_RUNNING);

    setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_STARTED);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::stopADDecoding()
{
    std::unique_lock<std::mutex> lock(mLock);

    return stopADDecoding_l(lock);
}

int AmlMpPlayerImpl::stopADDecoding_l(std::unique_lock<std::mutex>& lock)
{
    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);
    int ret;

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        if (mPlayer) {
            mPlayer->stopADDecoding();

            Aml_MP_AudioParams dummyADParam;
            memset(&dummyADParam, 0, sizeof(dummyADParam));
            dummyADParam.pid = AML_MP_INVALID_PID;
            dummyADParam.audioCodec = AML_MP_CODEC_UNKNOWN;
            mPlayer->setADParams(&dummyADParam, false);
        }

        if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_STARTED) {
            resetAudioCodec_l(true);
        }
    }

    // ensure stream stopped if mState isn't running or paused
    setDecodingState_l(AML_MP_STREAM_TYPE_AD, AML_MP_DECODING_STATE_STOPPED);

    ret = resetIfNeeded_l(lock);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::startSubtitleDecoding()
{
    std::unique_lock<std::mutex> _l(mLock);

    return startSubtitleDecoding_l();
}

int AmlMpPlayerImpl::startSubtitleDecoding_l()
{
    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);

    if (mState == STATE_IDLE) {
        if (prepare_l() < 0) {
            MLOGE("prepare failed!");
            return -1;
        }
    }

    if (mState == STATE_PREPARING) {
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_START_PENDING);
        return 0;
    }

    if (mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        mPlayer->setSubtitleParams(&mSubtitleParams);
    }

    int ret = mPlayer->startSubtitleDecoding();
    if (ret < 0) {
        MLOGE("%s failed!", __FUNCTION__);
        return -1;
    }

    setState_l(STATE_RUNNING);
    if (mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_STARTED);
    }

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::stopSubtitleDecoding()
{
    std::unique_lock<std::mutex> lock(mLock);

    return stopSubtitleDecoding_l(lock);
}

int AmlMpPlayerImpl::stopSubtitleDecoding_l(std::unique_lock<std::mutex>& lock) {
    MLOG();
    RETURN_IF(-1, mPlayer == nullptr);

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        if (mPlayer) {
            mPlayer->stopSubtitleDecoding();
        }
    }

    // ensure stream stopped if mState isn't running or paused
    setDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_DECODING_STATE_STOPPED);

    int ret = resetIfNeeded_l(lock);

    MLOG("end");
    return ret;
}

int AmlMpPlayerImpl::setSubtitleWindow(int x, int y, int width, int height)
{
    std::unique_lock<std::mutex> _l(mLock);
    MLOG("subtitle window:(%d %d %d %d)", x, y, width, height);

    mSubtitleWindow = {x, y, width, height};
    int ret = 0;

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        ret = mPlayer->setSubtitleWindow(x, y, width, height);
    }

    return ret;
}

//internal function
int AmlMpPlayerImpl::startDescrambling_l()
{

    if (mCasServiceType == AML_MP_CAS_SERVICE_TYPE_INVALID) {
        MLOGE("unknown cas type!");
        return -1;
    }

    if (mCasHandle == nullptr) {
        mCasHandle = AmlCasBase::create(mCasServiceType);
    }

    if (mCasHandle == nullptr) {
        MLOGE("create CAS handle failed!");
        return -1;
    }

    int ret = mCasHandle->startDescrambling(&mIptvCasParams);
    mCasHandle->getEcmPids(mEcmPids);

    return ret;
}

//internal function
int AmlMpPlayerImpl::stopDescrambling_l()
{

    if (mCasHandle) {
        mCasHandle->stopDescrambling();
        mCasHandle.clear();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
const char* AmlMpPlayerImpl::stateString(State state)
{
    switch (state) {
    case STATE_IDLE:
        return "STATE_IDLE";
    case STATE_PREPARING:
        return "STATE_PREPARING";
    case STATE_PREPARED:
        return "STATE_PREPARED";
    case STATE_RUNNING:
        return "STATE_RUNNING";
    case STATE_PAUSED:
        return "STATE_PAUSED";
    case STATE_STOPPED:
        return "STATE_STOPPED";
    default:
        break;
    }
    return "STATE_IDLE";
}

std::string AmlMpPlayerImpl::decodingStateString(uint32_t streamState)
{
    AML_MP_UNUSED(streamState);
    std::stringstream ss;
    bool hasValue = false;

    for (size_t i = 0; i < AML_MP_STREAM_TYPE_NB; ++i) {
        int value = (mStreamState >> i*kStreamStateBits) & kStreamStateMask;

        if (value != AML_MP_DECODING_STATE_STOPPED) {
            if (hasValue) ss << "|";

            ss << mpStreamType2Str((Aml_MP_StreamType)i);
            switch (value) {
            case AML_MP_DECODING_STATE_START_PENDING:
                ss << "_START_PENDING";
                break;

            case AML_MP_DECODING_STATE_STARTED:
                ss << "_STARTED";
                break;

            case AML_MP_DECODING_STATE_PAUSED:
                ss << "_PAUSED";
                break;

            default:
                break;
            }
            hasValue = true;
        }
    }

    return ss.str();
}

void AmlMpPlayerImpl::setState_l(State state)
{
    if (mState != state) {
        MLOGI("%s -> %s", stateString(mState), stateString(state));
        mLastState = mState;
        mState = state;
    }
}

void AmlMpPlayerImpl::setDecodingState_l(Aml_MP_StreamType streamType, int state)
{
    unsigned int offset = streamType * kStreamStateBits;

    if (offset >= sizeof(mStreamState) * 8) {
        MLOGE("streamType(%s) is overflow!", mpStreamType2Str(streamType));
        return;
    }

    mStreamState &= ~(kStreamStateMask<<offset);
    mStreamState |= state<<offset;
}

AML_MP_DecodingState AmlMpPlayerImpl::getDecodingState_l(Aml_MP_StreamType streamType)
{
    int offset = streamType * kStreamStateBits;

    return AML_MP_DecodingState((mStreamState >> offset) & kStreamStateMask);
}

int AmlMpPlayerImpl::prepare_l()
{
    MLOG();

    if (mCreateParams.drmMode != AML_MP_INPUT_STREAM_NORMAL) {
        if (!mIsStandaloneCas) {
            startDescrambling_l();
        }

        // in trick mode play, ecm may not match ts packets well
        // if use asynchronous wait ecm mode,
        // because inject speed is faster than normal play,
        // so force change to synchronous mode here.
        if (mVideoDecodeMode == AML_MP_VIDEO_DECODE_MODE_IONLY) {
            mWaitingEcmMode = kWaitingEcmSynchronous;
        } else {
            mWaitingEcmMode = mUserWaitingEcmMode;
        }
    }

    if (mPlayer == nullptr) {
        mPlayer = AmlPlayerBase::create(&mCreateParams, mInstanceId);
        if (mPlayer == nullptr) {
            MLOGE("AmlPlayerBase create failed!");
            return -1;
        }
    }

    if (mPlayer->initCheck() != AML_MP_OK) {
        MLOGE("AmlPlayerBase initCheck failed!");
        return -1;
    }

    if (mWorkMode >= 0) {
        MLOGI("mWorkMode: %s", mpPlayerWorkMode2Str(mWorkMode));
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_WORK_MODE, &mWorkMode);
    }

    mPlayer->registerEventCallback([](void* userData, Aml_MP_PlayerEventType event, int64_t param) {
        AmlMpPlayerImpl* thiz = static_cast<AmlMpPlayerImpl*>(userData);
        return thiz->notifyListener(event, param);
    }, this);

    if ((mSubtitleWindow.width >= 0) && (mSubtitleWindow.height >= 0)) {
        mPlayer->setSubtitleWindow(mSubtitleWindow.x, mSubtitleWindow.y, mSubtitleWindow.width, mSubtitleWindow.height);
    }

    if (mSurfaceHandle != nullptr) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_SURFACE_HANDLE, mSurfaceHandle);
    }
#ifdef ANDROID
    MLOGI("mNativeWindow:%p", mNativeWindow.get());
    if (mNativeWindow != nullptr) {
        mPlayer->setANativeWindow(mNativeWindow.get());
        setSidebandIfNeeded_l();
    }
#endif

    if (mVideoWindow.width >= 0 && mVideoWindow.height >= 0) {
        mPlayer->setVideoWindow(mVideoWindow.x, mVideoWindow.y, mVideoWindow.width, mVideoWindow.height);
    }

    if (mVideoTunnelId >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID, &mVideoTunnelId);
    }

    if (mSyncSource == AML_MP_AVSYNC_SOURCE_DEFAULT) {
        mSyncSource = AML_MP_AVSYNC_SOURCE_PCR;
    }

    mPlayer->setAVSyncSource(mSyncSource);

    if (mSyncSource == AML_MP_AVSYNC_SOURCE_PCR && mPcrPid != AML_MP_INVALID_PID) {
        mPlayer->setPcrPid(mPcrPid);
    }

    mPlayer->setPlaybackRate(mPlaybackRate);

    if (mVolume >= 0) {
        mPlayer->setVolume(mVolume);
    }

    if (mADVolume >= 0) {
        mPlayer->setADVolume(mADVolume);
    }

    if (mVideoAFDAspectMode >= 0) {
        setVideoAFDAspectMode_l(mVideoAFDAspectMode);
    }

    applyParameters_l();

    tryMonitorPidChange_l();
    if (tryWaitCodecId_l() | tryWaitEcm_l()) {
        setState_l(STATE_PREPARING);
        return 0;
    }

    setState_l(STATE_PREPARED);

    return 0;
}

void AmlMpPlayerImpl::openParser_l() {
    if (mParser == nullptr) {
        mParser = new Parser(mCreateParams.demuxId, mCreateParams.sourceType == AML_MP_INPUT_SOURCE_TS_DEMOD, AML_MP_DEMUX_TYPE_HARDWARE, mCreateParams.drmMode == AML_MP_INPUT_STREAM_SECURE_MEMORY);
        mParser->open();
        mParser->selectProgram(mVideoParams.pid, mAudioParams.pid);
        mParser->setEventCallback([this] (Parser::ProgramEventType event, int param1, int param2, void* data) {
                return programEventCallback(event, param1, param2, data);
        });
    }
}

bool AmlMpPlayerImpl::tryWaitEcm_l() {
    bool isNeedWaitEcm = false;
    if (mCreateParams.sourceType == AML_MP_INPUT_SOURCE_TS_MEMORY &&
        mCreateParams.drmMode == AML_MP_INPUT_STREAM_ENCRYPTED &&
        mCasHandle) {
        for (size_t i = 0; i < mEcmPids.size(); ++i) {
            int ecmPid = mEcmPids[i];
            if (ecmPid > 0 && ecmPid < AML_MP_INVALID_PID) {
                isNeedWaitEcm = true;
                break;
            }
        }
    }

    if (!isNeedWaitEcm) {
        return false;
    }

    resetDrmVariables_l();

    if (mWaitingEcmMode != kWaitingEcmASynchronous) {
        return true;
    }

    openParser_l();
    int lastEcmPid = AML_MP_INVALID_PID;
    for (size_t i = 0; i < mEcmPids.size(); ++i) {
        int ecmPid = mEcmPids[i];
        if (ecmPid <= 0 ||ecmPid >= AML_MP_INVALID_PID) {
            continue;
        }

        if (ecmPid != lastEcmPid) {
            mParser->removeSectionFilter(ecmPid);
            mParser->addSectionFilter(ecmPid, [] (int pid, size_t size, const uint8_t* data, void* userData) -> int {
                AmlMpPlayerImpl* playerImpl = (AmlMpPlayerImpl*)userData;
                if (playerImpl) {
                    playerImpl->programEventCallback(Parser::ProgramEventType::EVENT_ECM_DATA_PARSED, pid, size, (void*)data);
                }
                return 0;
            }, this, false);
            lastEcmPid = ecmPid;
        }
    }

    if (lastEcmPid == AML_MP_INVALID_PID) {
        MLOGE("no valid ecm pid!");
    }
    mPrepareWaitingType |= kPrepareWaitingEcm;
    return true;
}

bool AmlMpPlayerImpl::tryWaitCodecId_l() {
    if (mCreateParams.sourceType != AML_MP_INPUT_SOURCE_ES_MEMORY &&
        ((mVideoParams.videoCodec == AML_MP_CODEC_UNKNOWN && mVideoParams.pid != AML_MP_INVALID_PID) ||
        (mAudioParams.audioCodec == AML_MP_CODEC_UNKNOWN && mAudioParams.pid != AML_MP_INVALID_PID) ||
        (mADParams.audioCodec == AML_MP_CODEC_UNKNOWN && mADParams.pid != AML_MP_INVALID_PID))) {
        openParser_l();
        mParser->parseProgramInfoAsync();
        mPrepareWaitingType |= kPrepareWaitingCodecId;
        return true;
    }
    return false;
}

bool AmlMpPlayerImpl::tryMonitorPidChange_l() {
    if (mCreateParams.options & AML_MP_OPTION_MONITOR_PID_CHANGE) {
        openParser_l();
        mParser->parseProgramInfoAsync();
        return true;
    }
    return false;
}

void AmlMpPlayerImpl::programEventCallback(Parser::ProgramEventType event, int param1, int param2, void* data)
{
    AML_MP_UNUSED(param1);
    switch (event)
    {
        case Parser::ProgramEventType::EVENT_PROGRAM_PARSED:
        {
            ProgramInfo* programInfo = (ProgramInfo*)data;
            MLOGI("programEventCallback: program(programNumber=%d,pid= 0x%x) parsed", programInfo->programNumber, programInfo->pmtPid);
            programInfo->debugLog();

            std::lock_guard<std::mutex> _l(mLock);
            for (auto it : programInfo->videoStreams) {
                if (it.pid == mVideoParams.pid) {
                    mVideoParams.videoCodec = it.codecId;
                }
            }

            for (auto it : programInfo->audioStreams) {
                if (it.pid == mAudioParams.pid) {
                    mAudioParams.audioCodec = it.codecId;
                }
            }

            mPrepareWaitingType &= ~kPrepareWaitingCodecId;
            finishPreparingIfNeeded_l();

            break;
        }
        case Parser::ProgramEventType::EVENT_AV_PID_CHANGED:
        {
            Aml_MP_PlayerEventPidChangeInfo* info = (Aml_MP_PlayerEventPidChangeInfo*)data;
            MLOGI("programEventCallback: program(programNumber=%d,pid= 0x%x) pidchangeInfo: oldPid: 0x%x --> newPid: 0x%x",
                info->programNumber, info->programPid, info->oldStreamPid, info->newStreamPid);
            notifyListener(AML_MP_PLAYER_EVENT_PID_CHANGED, (uint64_t)data);
            break;
        }
        case Parser::ProgramEventType::EVENT_ECM_DATA_PARSED:
        {
            MLOGI("programEventCallback: ecmData parsed, size:%d", param2);
            uint8_t* ecmData = (uint8_t*)data;
            std::string ecmDataStr;
            char hex[3];
            for (int i = 0; i < param2; i++) {
                 snprintf(hex, sizeof(hex), "%02X", ecmData[i]);
                 ecmDataStr.append(hex);
                 ecmDataStr.append(" ");
            }
            MLOGI("programEventCallback: ecmPid: %d, ecmData: size:%d, hexStr:%s", param1, param2, ecmDataStr.c_str());

            {
                std::unique_lock<std::mutex> _l(mLock);
                if (mCasHandle && mWaitingEcmMode == kWaitingEcmASynchronous) {
                    mCasHandle->processEcm(true, param1, ecmData, param2);
                }

                mPrepareWaitingType &= ~kPrepareWaitingEcm;
                finishPreparingIfNeeded_l();
            }
            break;
        }
        default:
            break;
    }
}

int AmlMpPlayerImpl::finishPreparingIfNeeded_l()
{
    if (mState != STATE_PREPARING || mPrepareWaitingType != kPrepareWaitingNone) {
        return 0;
    }

    bool needPause = (mLastState == STATE_PAUSED);

    setState_l(STATE_PREPARED);

    if (getDecodingState_l(AML_MP_STREAM_TYPE_VIDEO) == AML_MP_DECODING_STATE_START_PENDING) {
        startVideoDecoding_l();
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AUDIO) == AML_MP_DECODING_STATE_START_PENDING) {
        startAudioDecoding_l();
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_SUBTITLE) == AML_MP_DECODING_STATE_START_PENDING) {
        startSubtitleDecoding_l();
        if (mSubtitleParams.subtitleCodec == AML_MP_SUBTITLE_CODEC_TELETEXT && mTeletextCtrlParam.event != AML_MP_TT_EVENT_INVALID) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_TELETEXT_CONTROL, &mTeletextCtrlParam);
        }
    }

    if (getDecodingState_l(AML_MP_STREAM_TYPE_AD) == AML_MP_DECODING_STATE_START_PENDING) {
        startADDecoding_l();
    }

    if (needPause) {
        pause_l();
    }

    return 0;
}

int AmlMpPlayerImpl::resetIfNeeded_l(std::unique_lock<std::mutex>& lock, bool clearCasSession)
{
    if (mStreamState == 0) {
        setState_l(STATE_STOPPED);
    } else {
        MLOGI("current decodingState:%s", decodingStateString(mStreamState).c_str());
    }

    if (mState == STATE_STOPPED) {
        reset_l(lock, clearCasSession);
    }

    return 0;
}

int AmlMpPlayerImpl::reset_l(std::unique_lock<std::mutex>& lock, bool clearCasSession)
{
    MLOG();
    if (mParser) {
        lock.unlock();
        mParser->close();
        lock.lock();

    }

    mParser.clear();
    mPlayer.clear();

    if (!mIsStandaloneCas) {
        stopDescrambling_l();
    }

    if (clearCasSession) {
        mIsStandaloneCas = false;
        mCasHandle.clear();
    }

    resetVariables_l();

    setState_l(STATE_IDLE);

    return 0;
}

int AmlMpPlayerImpl::applyParameters_l()
{
    mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_DECODE_MODE, &mVideoDecodeMode);

    if (mVideoDisplayMode >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE, &mVideoDisplayMode);
    }

    if (mBlackOut >= 0) {
        bool param = (0 == mBlackOut)?false:true;
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_BLACK_OUT, &param);
    }

    if (mVideoPtsOffset >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_PTS_OFFSET, &mVideoPtsOffset);
    }

    if (mAudioOutputMode >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_MODE, &mAudioOutputMode);
    }

    if (mAudioOutputDevice >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_DEVICE, &mAudioOutputDevice);
    }

    if (mAudioPtsOffset >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_PTS_OFFSET, &mAudioPtsOffset);
    }

    if (mAudioBalance >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_BALANCE, &mAudioBalance);
    }

    if (mAudioMute >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_MUTE, &mAudioMute);
    }

    if (mNetworkJitter >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_NETWORK_JITTER, &mNetworkJitter);
    }

    if (mVideoErrorRecoveryMode >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_ERROR_RECOVERY_MODE, &mVideoErrorRecoveryMode);
    }

    if (mADMixLevel.masterVolume >= 0 && mADMixLevel.slaveVolume >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AD_MIX_LEVEL, &mADMixLevel);
    }

    if (mUseTif != -1) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_USE_TIF, &mUseTif);
    }

    if (mVideoCrop.right >= 0 && mVideoCrop.bottom >= 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_CROP, &mVideoCrop);
    }

    if (mAudioLanguage.firstLanguage != 0 || mAudioLanguage.secondLanguage != 0) {
        mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_LANGUAGE, &mAudioLanguage);
    }

    return 0;
}

void AmlMpPlayerImpl::notifyListener(Aml_MP_PlayerEventType eventType, int64_t param)
{
    std::unique_lock<std::mutex> _l(mEventLock);
    mEventCbTid = gettid();

    if (mEventCb) {
        mEventCb(mUserData, eventType, param);
    } else {
        MLOGW("mEventCb is NULL, eventType: %s, param:%" PRId64, mpPlayerEventType2Str(eventType), param);
    }

    mEventCbTid = -1;
}

int AmlMpPlayerImpl::resetADCodec_l(bool callStart)
{
    int ret = 0;
    MLOG("callStart:%d", callStart);

    ret = mPlayer->stopADDecoding();
    if (ret < 0) {
        MLOGW("stopADDecoding failed while resetADCodec");
    }

    if (mADParams.pid != AML_MP_INVALID_PID) {
        mPlayer->setADParams(&mADParams, true);
    }

    if (callStart) {
        ret = mPlayer->startADDecoding();
    }

    return ret;
}

int AmlMpPlayerImpl::resetAudioCodec_l(bool callStart)
{
    int ret = 0;
    MLOG("callStart:%d", callStart);

    ret = mPlayer->stopAudioDecoding();
    if (ret < 0) {
        MLOGW("stopAudioDecoding failed while resetAudioCodec");
    }

    if (mAudioParams.pid != AML_MP_INVALID_PID) {
        mPlayer->setAudioParams(&mAudioParams);
        if (mAudioPresentationId >= 0) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID, &mAudioPresentationId);
        }
        if (mSPDIFStatus != -1) {
            mPlayer->setParameter(AML_MP_PLAYER_PARAMETER_SPDIF_PROTECTION, &mSPDIFStatus);
        }
    }

    if (callStart) {
        ret = mPlayer->startAudioDecoding();
    }

    return ret;
}

int AmlMpPlayerImpl::switchDecodeMode_l(Aml_MP_VideoDecodeMode decodeMode, std::unique_lock<std::mutex>& lock) {

    int ret = 0;
    MLOG("decodeMode: %s", mpVideoDecodeMode2Str(decodeMode));

    if (mVideoDecodeMode == decodeMode) {
        MLOGI("Decode mode not change, no need do process");
        return 0;
    }

    mVideoDecodeMode = decodeMode;

    if (mState != STATE_RUNNING) {
        MLOGI("Now not in play, no Need to do process");
        return 0;
    }

    //Not clear cas session while switch decode mode
    ret = stop_l(lock, false);
    //When start video decoding will set work mode to player, no need to set here
    if (AML_MP_VIDEO_DECODE_MODE_NONE != mVideoDecodeMode) {
        ret += startVideoDecoding_l();
    } else {
        ret += start_l();
    }

    return ret;
}

void AmlMpPlayerImpl::statisticWriteDataRate_l(size_t size)
{
    mLastBytesWritten += size;

    int64_t nowUs = AmlMpEventLooper::GetNowUs();
    if (mLastWrittenTimeUs == 0) {
        mLastWrittenTimeUs = nowUs;
    } else {
        int64_t diffUs = nowUs - mLastWrittenTimeUs;
        if (diffUs > 2 * 1000000ll) {
            int64_t bitrate = mLastBytesWritten * 1000000 / diffUs;
            MLOGI("writeData rate:%.2fKB/s", bitrate/1024.0);

            mLastWrittenTimeUs = nowUs;
            mLastBytesWritten = 0;

            collectBuffingInfos_l();
        }
    }
}

void AmlMpPlayerImpl::collectBuffingInfos_l()
{
    Aml_MP_BufferStat bufferStat;
    mPlayer->getBufferStat(&bufferStat);

    int64_t vpts, apts;
    mPlayer->getCurrentPts(AML_MP_STREAM_TYPE_VIDEO, &vpts);
    mPlayer->getCurrentPts(AML_MP_STREAM_TYPE_AUDIO, &apts);

    if (mVideoParams.pid != AML_MP_INVALID_PID) {
        MLOGI("Video(%#x) buffer stat:%d/%d, %.2fms, pts:%f", mVideoParams.pid, bufferStat.videoBuffer.dataLen, bufferStat.videoBuffer.size, bufferStat.videoBuffer.bufferedMs*1.0, vpts/9e4);
    }

    if (mAudioParams.pid != AML_MP_INVALID_PID) {
        MLOGI("Audio(%#x) buffer stat:%d/%d, %.2fms, pts:%f", mAudioParams.pid, bufferStat.audioBuffer.dataLen, bufferStat.audioBuffer.size, bufferStat.audioBuffer.bufferedMs*1.0, apts/9e4);
    }
}

void AmlMpPlayerImpl::resetVariables_l()
{
    mLastBytesWritten = 0;
    mLastWrittenTimeUs = 0;
    mAudioStoppedInSwitching = false;

    resetDrmVariables_l();
}

void AmlMpPlayerImpl::resetDrmVariables_l()
{
    mFirstEcmWritten = false;
    mTsBuffer.reset();
    mWriteBuffer->setRange(0, 0);
}

int AmlMpPlayerImpl::getDecodingState(Aml_MP_StreamType streamType, AML_MP_DecodingState* streamState) {
    std::unique_lock<std::mutex> _l(mLock);

    *streamState = getDecodingState_l(streamType);

    return AML_MP_OK;
}

void AmlMpPlayerImpl::increaseDmxSecMemSize() {
    MLOGI("supportPIP=%d supportFCC=%d secMemSize=%d isCasProject=%d", AmlMpConfig::instance().mCasPipSupport,
        AmlMpConfig::instance().mCasFCCSupport, AmlMpConfig::instance().mSecMemSize, AmlMpConfig::instance().mCasType != "none");

    if (AmlMpConfig::instance().mCasType == "none") {
        return ;
    }

    if (AmlMpConfig::instance().mCasFCCSupport) {
        if (AmlMpConfig::instance().mSecMemSize < FCC_DEMUX_MEM_SEC_SIZE)
            AmlMpConfig::instance().mSecMemSize = FCC_DEMUX_MEM_SEC_SIZE;
    } else if (AmlMpConfig::instance().mCasPipSupport) {
        if (AmlMpConfig::instance().mSecMemSize < PIP_DEMUX_MEM_SEC_SIZE)
            AmlMpConfig::instance().mSecMemSize = PIP_DEMUX_MEM_SEC_SIZE;
    } else {
        return ;
    }

    char valuebuf[64] = {0};
    sprintf(valuebuf, "%d %d", DEFAULT_DEMUX_MEM_SEC_LEVEL>>10, AmlMpConfig::instance().mSecMemSize);

    int ret = amsysfs_set_sysfs_str("/sys/class/stb/dmc_mem", valuebuf);
    MLOGI("dmc mem ret=%d level=%d value=%d", ret, DEFAULT_DEMUX_MEM_SEC_LEVEL,
        AmlMpConfig::instance().mSecMemSize);

    return;
}

void AmlMpPlayerImpl::recoverDmxSecMemSize() {
    if (AmlMpConfig::instance().mCasType != "none"
        && (AmlMpConfig::instance().mCasFCCSupport || AmlMpConfig::instance().mCasPipSupport)) {
        char valuebuf[64] = {0};
        sprintf(valuebuf, "%d %d", DEFAULT_DEMUX_MEM_SEC_LEVEL>>10, DEFAULT_DEMUX_MEM_SEC_SIZE);
        int ret = amsysfs_set_sysfs_str("/sys/class/stb/dmc_mem", valuebuf);
        MLOGI("recover dmc mem ret=%d level=%d value=%d", ret,
            DEFAULT_DEMUX_MEM_SEC_LEVEL, DEFAULT_DEMUX_MEM_SEC_SIZE);
    }
}

int AmlMpPlayerImpl::setADVolume(float volume) {
    std::unique_lock<std::mutex> _l(mLock);
    int ret = 0;
    if (volume < 0) {
        MLOGI("volume is %f, set to 0.0", volume);
        volume = 0.0;
    } else if (volume > 100) {
        MLOGI("volume is %f, set to 100.0", volume);
        volume = 100.0;
    }
    mADVolume = volume;

    if (mState == STATE_RUNNING || mState == STATE_PAUSED) {
        RETURN_IF(-1, mPlayer == nullptr);
        ret = mPlayer->setADVolume(volume);
    }
    return ret;
}


int AmlMpPlayerImpl::getADVolume(float* volume)
{
    std::unique_lock<std::mutex> _l(mLock);
    RETURN_IF(-1, mPlayer == nullptr);

    return mPlayer->getADVolume(volume);
}

int AmlMpPlayerImpl::enableAFD_l(bool enable)
{
    MLOGI("[%s] enable: %d", __FUNCTION__, enable);

    int ret = 0;
    char enableParam[64] = {0};

    if (enable) {
        RETURN_IF(-1, mPlayer == nullptr);
        sprintf(enableParam, "%d %d %d", 0, mPlayer->getPlayerId(), 1);
    } else {
        sprintf(enableParam, "%d %d %d", 0, 0, 0);
    }
    ret = amsysfs_set_sysfs_str("/sys/class/afd_module/enable", enableParam);

    return ret;
}

int AmlMpPlayerImpl::setVideoAFDAspectMode_l(Aml_MP_VideoAFDAspectMode aspectMode)
{
    int ret = 0;
    char aspectModeParam[64] = {0};

    if (aspectMode >= AML_MP_VIDEO_AFD_ASPECT_MODE_AUTO && aspectMode < AML_MP_VIDEO_AFD_ASPECT_MODE_MAX) {
        ret = enableAFD_l(true);
        sprintf(aspectModeParam, "%d %d", 0, aspectMode);
    } else {
        MLOGE("[%s] Invalid aspect mode: %d", __FUNCTION__, aspectMode);
        ret = enableAFD_l(false);
        sprintf(aspectModeParam, "%d %d", 0, AML_MP_VIDEO_AFD_ASPECT_MODE_NONE);
    }
    ret += amsysfs_set_sysfs_str("/sys/class/afd_module/aspect_mode", aspectModeParam);

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
}
