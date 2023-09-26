/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlDVRPlayer"
#include "AmlDVRPlayer.h"
#include <Aml_MP/Dvr.h>
#include <Aml_MP/Aml_MP.h>
#include <utils/AmlMpHandle.h>
#include <utils/AmlMpUtils.h>
#include <utils/AmlMpLog.h>
#ifdef ANDROID
#ifndef __ANDROID_VNDK__
#include <gui/Surface.h>
#endif

#include <amlogic/am_gralloc_ext.h>
#endif

namespace aml_mp {

static Aml_MP_DVRPlayerState convertToMpDVRPlayerState(DVR_PlaybackPlayState_t state);
static Aml_MP_DVRPlayerSegmentFlag convertToMpDVRSegmentFlag(DVR_PlaybackSegmentFlag_t flag);
static void convertToMpDVRPlayerStatus(Aml_MP_DVRPlayerStatus* mpStatus, DVR_WrapperPlaybackStatus_t* dvrStatus);

///////////////////////////////////////////////////////////////////////////////
AmlDVRPlayer::AmlDVRPlayer(Aml_MP_DVRPlayerBasicParams* basicParams, Aml_MP_DVRPlayerDecryptParams* decryptParams)
{
    snprintf(mName, sizeof(mName), "%s", LOG_TAG);
    MLOG();

    memset(&mPlaybackOpenParams, 0, sizeof(DVR_WrapperPlaybackOpenParams_t));

    memset(&mVideoParams, 0, sizeof(mVideoParams));
    mVideoParams.pid = AML_MP_INVALID_PID;
    mVideoParams.videoCodec = AML_MP_CODEC_UNKNOWN;

    memset(&mAudioParams, 0, sizeof(mAudioParams));
    mAudioParams.pid = AML_MP_INVALID_PID;
    mAudioParams.audioCodec = AML_MP_CODEC_UNKNOWN;

    memset(&mADParams, 0, sizeof(mADParams));
    mADParams.pid = AML_MP_INVALID_PID;
    mADParams.audioCodec = AML_MP_CODEC_UNKNOWN;

    memset(&mSubtitleParams, 0, sizeof(mSubtitleParams));
    mSubtitleParams.pid = AML_MP_INVALID_PID;
    mSubtitleParams.subtitleCodec = AML_MP_CODEC_UNKNOWN;

    setBasicParams(basicParams);
    mRecStartTime = 0;
    mLimit = 0;
    mAudioPresentationId = -1;
    mIsEncryptStream = basicParams->drmMode != AML_MP_INPUT_STREAM_NORMAL;
    mInputStreamType = basicParams->drmMode;

    if (decryptParams != nullptr) {
        setDecryptParams(decryptParams);
    }

    createMpPlayerIfNeeded();
}

AmlDVRPlayer::~AmlDVRPlayer()
{
    MLOG();
}

int AmlDVRPlayer::registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    AML_MP_TRACE(10);
    mEventCb = cb;
    mEventUserData = userData;
    if (mMpPlayerHandle) {
        Aml_MP_Player_RegisterEventCallBack(mMpPlayerHandle, mEventCb, mEventUserData);
    }
    return 0;
}

int AmlDVRPlayer::setStreams(Aml_MP_DVRStreamArray* streams)
{
    MLOG();
    int ret = 0;
    setStreamsCommon(streams);
    if (mDVRPlayerHandle) {
        if (mAudioPresentationId > -1) {
            ret = dvr_wrapper_set_ac4_preselection_id(mDVRPlayerHandle, mAudioPresentationId);
            if (ret < 0) {
                MLOGE("set ac4 preselection id failed!");
            }
        }
        ret = dvr_wrapper_update_playback(mDVRPlayerHandle, &mPlayPids);
        if (ret < 0) {
            MLOGE("update playback failed!");
        }
    }
    return ret;
}

int AmlDVRPlayer::onlySetStreams(Aml_MP_DVRStreamArray* streams)
{
    MLOG();
    int ret = 0;
    setStreamsCommon(streams);
    if (mDVRPlayerHandle) {
        if (mAudioPresentationId > -1) {
            ret = dvr_wrapper_set_ac4_preselection_id(mDVRPlayerHandle, mAudioPresentationId);
            if (ret < 0) {
                MLOGE("set ac4 preselection id failed!");
            }
        }
        ret = dvr_wrapper_only_update_playback(mDVRPlayerHandle, &mPlayPids);
        if (ret < 0) {
            MLOGE("update playback failed!");
        }
    }
    return ret;
}

int AmlDVRPlayer::start(bool initialPaused)
{
    MLOG();

    int ret = createMpPlayerIfNeeded();
    if (ret != 0) {
        return -1;
    }
    ret = Aml_MP_Player_RegisterEventCallBack(mMpPlayerHandle, mEventCb, mEventUserData);

#ifdef ANDROID
    ret = Aml_MP_Player_SetANativeWindow(mMpPlayerHandle, mNativeWindow.get());
#endif

    ret = Aml_MP_Player_Start(mMpPlayerHandle);

    Playback_DeviceHandle_t mTsPlayerHandle;
    ret = Aml_MP_Player_GetParameter(mMpPlayerHandle, AML_MP_PLAYER_PARAMETER_TSPLAYER_HANDLE, (void *)&mTsPlayerHandle);
    MLOGI(" MpPlayer get ts player handle %s, result(%d)", (ret)? "FAIL" : "OK", ret);
    if (ret != 0) {
        return ret;
    }

    mPlaybackOpenParams.playback_handle = (Playback_DeviceHandle_t)mTsPlayerHandle;
    mPlaybackOpenParams.event_fn = [](DVR_PlaybackEvent_t event, void* params, void* userData) {
        AmlDVRPlayer* player = static_cast<AmlDVRPlayer*>(userData);
        return player->eventHandlerLibDVR(event, params);
    };
    mPlaybackOpenParams.event_userdata = this;

    //apply parameters
    mPlaybackOpenParams.vendor = (DVR_PlaybackVendor_t)mVendorID;
    int error = dvr_wrapper_open_playback(&mDVRPlayerHandle, &mPlaybackOpenParams);
    if (error < 0) {
        MLOGE("open playback failed!");
        return error;
    }

    if (mIsEncryptStream) {
        dvr_wrapper_set_playback_secure_buffer(mDVRPlayerHandle, mSecureBuffer, mSecureBufferSize);
    }

    if (mRecStartTime > 0) {
        dvr_wrapper_setlimit_playback(mDVRPlayerHandle, mRecStartTime, mLimit);
    }
    DVR_PlaybackFlag_t play_flag = initialPaused ? DVR_PLAYBACK_STARTED_PAUSEDLIVE : (DVR_PlaybackFlag_t)0;
    error = dvr_wrapper_start_playback(mDVRPlayerHandle, play_flag, &mPlayPids);
    return error;
}

int AmlDVRPlayer::stop()
{
    MLOG();

    int ret = 0;

    ret = dvr_wrapper_stop_playback(mDVRPlayerHandle);
    if (ret < 0) {
        MLOGE("ts player stop playback failed!");
    }

    ret = Aml_MP_Player_Stop(mMpPlayerHandle);
    if (ret < 0) {
        MLOGE("mp player stop playback failed!");
    }

    //Add support cas
    if (mIsEncryptStream) {
    }

    ret = dvr_wrapper_close_playback(mDVRPlayerHandle);
    if (ret < 0) {
        MLOGE("ts player close playback failed!");
    }

    if (mMpPlayerHandle != AML_MP_INVALID_HANDLE) {
        AML_MP_PLAYER tmpHandle = mMpPlayerHandle;
        mMpPlayerHandle = AML_MP_INVALID_HANDLE;
        ret = Aml_MP_Player_Destroy(tmpHandle);
        if (ret < 0) {
            MLOGE("mp player close playback failed!");
        }
    }
    return 0;
}

int AmlDVRPlayer::pause()
{
    MLOG();

    int ret = 0;

    ret = dvr_wrapper_pause_playback(mDVRPlayerHandle);
    if (ret < 0) {
        MLOGE("ts player pause playback failed!");
    }

    ret = Aml_MP_Player_Pause(mMpPlayerHandle);
    if (ret < 0) {
        MLOGE("mp player pause playback failed!");
    }

    return ret;
}

int AmlDVRPlayer::resume()
{
    MLOG();

    int ret = 0;

    ret = dvr_wrapper_resume_playback(mDVRPlayerHandle);
    if (ret < 0) {
        MLOGE("ts player resume playback failed!");
    }

    ret = Aml_MP_Player_Resume(mMpPlayerHandle);
    if (ret < 0) {
        MLOGE("mp player resume playback failed!");
    }

    return ret;
}

int AmlDVRPlayer::setLimit(uint32_t time, uint32_t limit)
{
    MLOG("rec start time:%u limit:%u", time, limit);
    mRecStartTime = time;
    mLimit = limit;
    if (mRecStartTime > 0) {
        dvr_wrapper_setlimit_playback(mDVRPlayerHandle, mRecStartTime, mLimit);
    }
    return 0;
}

int AmlDVRPlayer::seek(int timeOffset)
{
    MLOG("timeOffset:%d", timeOffset);

    int ret = dvr_wrapper_seek_playback(mDVRPlayerHandle, timeOffset);
    if (ret < 0) {
        MLOGE("seek playback %d failed!", timeOffset);
    }

    return ret;
}

int AmlDVRPlayer::setPlaybackRate(float rate)
{
    MLOG("rate:%f", rate);
    int ret = 0;

    ret = dvr_wrapper_set_playback_speed(mDVRPlayerHandle, rate * 100);
    if (ret < 0) {
        MLOGE("set playback speed failed!");
    }

    return 0;
}

int AmlDVRPlayer::getStatus(Aml_MP_DVRPlayerStatus* status)
{
    int ret = -1;
    DVR_WrapperPlaybackStatus_t dvrStatus;
    ret = dvr_wrapper_get_playback_status(mDVRPlayerHandle, &dvrStatus);
    if (ret < 0) {
        MLOGE("get playback status failed!");
        return ret;
    }

    convertToMpDVRPlayerStatus(status, &dvrStatus);

    return ret;
}

int AmlDVRPlayer::showVideo()
{
    MLOG();

    int ret = -1;
    ret = Aml_MP_Player_ShowVideo(mMpPlayerHandle);

    return ret;
}

int AmlDVRPlayer::hideVideo()
{
    MLOG();

    int ret = -1;
    ret = Aml_MP_Player_HideVideo(mMpPlayerHandle);

    return ret;
}

int AmlDVRPlayer::setVolume(float volume)
{
    MLOG("volume:%f", volume);

    int ret = -1;
    ret = Aml_MP_Player_SetVolume(mMpPlayerHandle, volume);

    return ret;
}

int AmlDVRPlayer::getVolume(float* volume)
{
    MLOG();

    int ret = -1;
    ret = Aml_MP_Player_GetVolume(mMpPlayerHandle, volume);

    return ret;
}

int AmlDVRPlayer::setParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    RETURN_IF(-1, parameter == nullptr);

    int ret = 0;
    switch (key) {
        case AML_MP_PLAYER_PARAMETER_VENDOR_ID:
            mVendorID = *(int*)parameter;
            break;
        case AML_MP_PLAYER_PARAMETER_AUDIO_PRESENTATION_ID:
        {
            mAudioPresentationId = *(int32_t*)parameter;
            break;
        }
        case AML_MP_PLAYER_PARAMETER_LIBDVR_FAKE_PID:
        {
            const int fake_pid = *(int*)parameter;
            dvr_wrapper_property_set("vendor.tv.dtv.fake_pid",std::to_string(fake_pid).c_str());
            break;
        }
        default:
            break;

    }
    ret = Aml_MP_Player_SetParameter(mMpPlayerHandle, key, parameter);
    return ret;
}

int AmlDVRPlayer::getParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    RETURN_IF(-1, parameter == nullptr);

    int ret = -1;
    ret = Aml_MP_Player_GetParameter(mMpPlayerHandle, key, parameter);
    return ret;
}

int AmlDVRPlayer::setANativeWindow(ANativeWindow* nativeWindow) {
    MLOG();

#ifdef ANDROID
    mNativeWindow = nativeWindow;
    MLOGI("PVR setAnativeWindow: %p, mNativewindow: %p", nativeWindow, mNativeWindow.get());
#endif

    int ret = -1;
    ret = Aml_MP_Player_SetANativeWindow(mMpPlayerHandle, nativeWindow);
    return ret;
}

int AmlDVRPlayer::setVideoWindow(int x, int y, int width, int height)
{
    MLOG("x:%d, y:%d, width:%d, height:%d", x, y, width, height);

    int ret = -1;
    ret = Aml_MP_Player_SetVideoWindow(mMpPlayerHandle, x, y, width, height);
    return ret;
}

int AmlDVRPlayer::setADVolume(float volume)
{
    MLOG("ad volume:%f", volume);

    int ret = -1;
    ret = Aml_MP_Player_SetADVolume(mMpPlayerHandle, volume);
    return ret;
}

int AmlDVRPlayer::getADVolume(float* volume)
{
    MLOG();

    if (volume == NULL) {
       return -1;
    }

    int ret = -1;
    ret = Aml_MP_Player_GetADVolume(mMpPlayerHandle, volume);
    return ret;
}

////////////////////////////////////////////////////////////////////////////////
int AmlDVRPlayer::setBasicParams(Aml_MP_DVRPlayerBasicParams* basicParams)
{
    mPlaybackOpenParams.dmx_dev_id = basicParams->demuxId;
    memcpy(&(mPlaybackOpenParams.location), &(basicParams->location), DVR_MAX_LOCATION_SIZE);
    mPlaybackOpenParams.block_size = basicParams->blockSize;
    mPlaybackOpenParams.is_timeshift = basicParams->isTimeShift;
    mPlaybackOpenParams.is_notify_time = basicParams->isNotifyTime;
    return 0;
}

int AmlDVRPlayer::setDecryptParams(Aml_MP_DVRPlayerDecryptParams * decryptParams)
{
    mPlaybackOpenParams.crypto_fn = (DVR_CryptoFunction_t)decryptParams->cryptoFn;
    mPlaybackOpenParams.crypto_data = decryptParams->cryptoData;

    mSecureBuffer = decryptParams->secureBuffer;
    mSecureBufferSize = decryptParams->secureBufferSize;

    MLOGI("mSecureBuffer:%p, mSecureBufferSize:%zu", mSecureBuffer, mSecureBufferSize);

    mPlaybackOpenParams.clearkey = decryptParams->clearKey;
    mPlaybackOpenParams.cleariv = decryptParams->clearIV;
    mPlaybackOpenParams.keylen = decryptParams->keyLength;

    return 0;
}

int AmlDVRPlayer::createMpPlayerIfNeeded()
{
    if (mMpPlayerHandle != AML_MP_INVALID_HANDLE) {
        return 0;
    }

    Aml_MP_PlayerCreateParams mMpPlayerInitparam;
    memset(&mMpPlayerInitparam, 0, sizeof(mMpPlayerInitparam));
    mMpPlayerInitparam.channelId = AML_MP_CHANNEL_ID_AUTO;
    mMpPlayerInitparam.demuxId = (Aml_MP_DemuxId)mPlaybackOpenParams.dmx_dev_id;
    mMpPlayerInitparam.sourceType = AML_MP_INPUT_SOURCE_TS_MEMORY;
    mMpPlayerInitparam.drmMode = mInputStreamType;
    mMpPlayerInitparam.options = AML_MP_OPTION_DVR_PLAYBACK;
    MLOGI("drmMode: %d", mMpPlayerInitparam.drmMode);

    AML_MP_PLAYER playerHandle;
    int ret = Aml_MP_Player_Create(&mMpPlayerInitparam, &playerHandle);
    if (ret != AML_MP_OK) {
        MLOGE("Create MpPlayer fail");
    } else {
       mMpPlayerHandle = playerHandle;
    }

    return ret;
}

DVR_Result_t AmlDVRPlayer::eventHandlerLibDVR(DVR_PlaybackEvent_t event, void* params)
{
    DVR_Result_t ret = DVR_SUCCESS;
    Aml_MP_DVRPlayerStatus mpStatus;

    convertToMpDVRPlayerStatus(&mpStatus, (DVR_WrapperPlaybackStatus_t*)params);

    switch (event) {
    case DVR_PLAYBACK_EVENT_ERROR:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_ERROR, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_TRANSITION_OK:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_TRANSITION_OK, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_TRANSITION_FAILED:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_TRANSITION_FAILED, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_KEY_FAILURE:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_KEY_FAILURE, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_NO_KEY:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_NO_KEY, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_REACHED_BEGIN:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_REACHED_BEGIN, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_REACHED_END:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_REACHED_END, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_NOTIFY_PLAYTIME:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_NOTIFY_PLAYTIME, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_FIRST_FRAME:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_PLAYER_EVENT_FIRST_FRAME, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_TIMESHIFT_FR_REACHED_BEGIN:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_TIMESHIFT_FR_REACHED_BEGIN, (int64_t)&mpStatus);
        break;

    case DVR_PLAYBACK_EVENT_TIMESHIFT_FF_REACHED_END:
        if (mEventCb) mEventCb(mEventUserData, AML_MP_DVRPLAYER_EVENT_TIMESHIFT_FF_REACHED_END, (int64_t)&mpStatus);
        break;

    default:
        MLOG("unhandled event:%d", event);
        break;
    }

    return ret;
}

int AmlDVRPlayer::getMpPlayerHandle(AML_MP_PLAYER* handle)
{
    *handle = AML_MP_INVALID_HANDLE;
    if (mMpPlayerHandle != AML_MP_INVALID_HANDLE) {
        *handle = mMpPlayerHandle;
        return 0;
    }
    return -1;
}

int AmlDVRPlayer::setStreamsCommon(Aml_MP_DVRStreamArray* streams)
{
    int ret = 0;
    Aml_MP_DVRStream* videoStream   = &streams->streams[AML_MP_DVR_VIDEO_INDEX];
    Aml_MP_DVRStream* audioStream   = &streams->streams[AML_MP_DVR_AUDIO_INDEX];
    Aml_MP_DVRStream* adStream      = &streams->streams[AML_MP_DVR_AD_INDEX];
    Aml_MP_DVRStream* subStream     = &streams->streams[AML_MP_DVR_SUBTITLE_INDEX];

    MLOG("video:pid %#x, codecId: %s, audio:pid %#x, codecId: %s, ad:pid: %#x, codecId: %s, sub:pid %#x, codecId: %s",
            videoStream->pid, mpCodecId2Str(videoStream->codecId),
            audioStream->pid, mpCodecId2Str(audioStream->codecId),
            adStream->pid, mpCodecId2Str(adStream->codecId),
            subStream->pid, mpCodecId2Str(subStream->codecId));

    memset(&mPlayPids, 0, sizeof(mPlayPids));
    mPlayPids.video.type = DVR_STREAM_TYPE_VIDEO;
    mPlayPids.video.pid = videoStream->pid;
    mPlayPids.video.format = convertToDVRVideoFormat(videoStream->codecId);

    mPlayPids.audio.type = DVR_STREAM_TYPE_AUDIO;
    mPlayPids.audio.pid = audioStream->pid;
    mPlayPids.audio.format = convertToDVRAudioFormat(audioStream->codecId);

    mPlayPids.ad.type = DVR_STREAM_TYPE_AD;
    mPlayPids.ad.pid = adStream->pid;
    mPlayPids.ad.format = convertToDVRAudioFormat(adStream->codecId);

    //If audio and AD pid is same, set invalid pid for AD
    if (mPlayPids.ad.pid == mPlayPids.audio.pid) {
        mPlayPids.ad.pid = AML_MP_INVALID_PID;
        mPlayPids.ad.format = (DVR_AudioFormat_t)(-1);
    }

    //set audio/video/ad/subtitle param to mp player
    mVideoParams.pid = videoStream->pid;
    mVideoParams.videoCodec = videoStream->codecId;
    mVideoParams.secureLevel = videoStream->secureLevel;

    mAudioParams.pid = audioStream->pid;
    mAudioParams.audioCodec = audioStream->codecId;
    mAudioParams.secureLevel = audioStream->secureLevel;

    mADParams.pid = adStream->pid;
    mADParams.audioCodec = adStream->codecId;
    mADParams.secureLevel = adStream->secureLevel;
    if (mADParams.pid == mAudioParams.pid) {
        mADParams.pid = AML_MP_INVALID_PID;
        mADParams.audioCodec = (Aml_MP_CodecID)-1;
        mADParams.secureLevel = AML_MP_DEMUX_MEM_SEC_NONE;
    }

    mSubtitleParams.pid = subStream->pid;
    mSubtitleParams.subtitleCodec = subStream->codecId;

    if (mMpPlayerHandle) {
        ret = Aml_MP_Player_SetVideoParams(mMpPlayerHandle, &mVideoParams);
        ret = Aml_MP_Player_SetAudioParams(mMpPlayerHandle, &mAudioParams);
        if (mADParams.pid != AML_MP_INVALID_PID) {
            ret = Aml_MP_Player_SetADParams(mMpPlayerHandle, &mADParams);
        }
        if (mSubtitleParams.subtitleCodec != AML_MP_VIDEO_CODEC_BASE &&
            mSubtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
            ret = Aml_MP_Player_SetSubtitleParams(mMpPlayerHandle, &mSubtitleParams);
        }
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
Aml_MP_DVRPlayerState convertToMpDVRPlayerState(DVR_PlaybackPlayState_t state)
{
    switch (state) {
    case DVR_PLAYBACK_STATE_START:
        return AML_MP_DVRPLAYER_STATE_START;
        break;

    case DVR_PLAYBACK_STATE_STOP:
        return AML_MP_DVRPLAYER_STATE_STOP;
        break;

    case DVR_PLAYBACK_STATE_PAUSE:
        return AML_MP_DVRPLAYER_STATE_PAUSE;
        break;

    case DVR_PLAYBACK_STATE_FF:
        return AML_MP_DVRPLAYER_STATE_FF;
        break;

    case DVR_PLAYBACK_STATE_FB:
        return AML_MP_DVRPLAYER_STATE_FB;
        break;

    default:
        break;
    }

    return AML_MP_DVRPLAYER_STATE_START;
}

Aml_MP_DVRPlayerSegmentFlag convertToMpDVRSegmentFlag(DVR_PlaybackSegmentFlag_t flag)
{
    switch (flag) {
    case DVR_PLAYBACK_SEGMENT_ENCRYPTED:
        return AML_MP_DVRPLAYER_SEGMENT_ENCRYPTED;
        break;

    case DVR_PLAYBACK_SEGMENT_DISPLAYABLE:
        return AML_MP_DVRPLAYER_SEGMENT_DISPLAYABLE;
        break;

    case DVR_PLAYBACK_SEGMENT_CONTINUOUS:
        return AML_MP_DVRPLAYER_SEGMENT_CONTINUOUS;
        break;

    default:
        break;
    }

    return AML_MP_DVRPLAYER_SEGMENT_DISPLAYABLE;
}

static void convertToMpDVRPlayerStatus(Aml_MP_DVRPlayerStatus* mpStatus, DVR_WrapperPlaybackStatus_t* dvrStatus)
{
    mpStatus->state = convertToMpDVRPlayerState(dvrStatus->state);
    convertToMpDVRSourceInfo(&mpStatus->infoCur, &dvrStatus->info_cur);
    convertToMpDVRSourceInfo(&mpStatus->infoFull, &dvrStatus->info_full);

    convertToMpDVRStream(&mpStatus->pids.streams[AML_MP_DVR_VIDEO_INDEX], &dvrStatus->pids.video);
    convertToMpDVRStream(&mpStatus->pids.streams[AML_MP_DVR_AUDIO_INDEX], &dvrStatus->pids.audio);
    convertToMpDVRStream(&mpStatus->pids.streams[AML_MP_DVR_AD_INDEX], &dvrStatus->pids.ad);
    convertToMpDVRStream(&mpStatus->pids.streams[AML_MP_DVR_SUBTITLE_INDEX], &dvrStatus->pids.subtitle);
    convertToMpDVRStream(&mpStatus->pids.streams[AML_MP_DVR_PCR_INDEX], &dvrStatus->pids.pcr);

    mpStatus->pids.nbStreams = AML_MP_DVR_STREAM_NB;
    mpStatus->speed = dvrStatus->speed;
    mpStatus->flags = convertToMpDVRSegmentFlag(dvrStatus->flags);
    convertToMpDVRSourceInfo(&mpStatus->infoObsolete, &dvrStatus->info_obsolete);
}

}
