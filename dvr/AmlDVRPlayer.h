/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_DVR_PLAYER_H_
#define _AML_DVR_PLAYER_H_

#include <Aml_MP/Dvr.h>
#include <utils/AmlMpRefBase.h>
#include <utils/AmlMpHandle.h>
#include <utils/AmlMpUtils.h>
#include "AmTsPlayer.h"
#ifdef ANDROID
#include <system/window.h>
#include <utils/RefBase.h>
#include <utils/StrongPointer.h>
#endif

namespace android {
class NativeHandle;
}

extern "C" int dvr_wrapper_set_playback_secure_buffer (DVR_WrapperPlayback_t playback,  uint8_t *p_secure_buf, uint32_t len);

namespace aml_mp {
using android::NativeHandle;

class AmlDVRPlayer final : public AmlMpHandle
{
public:
    AmlDVRPlayer(Aml_MP_DVRPlayerBasicParams* basicParams, Aml_MP_DVRPlayerDecryptParams* decryptParams = nullptr);
    ~AmlDVRPlayer();
    int registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData);
    int setStreams(Aml_MP_DVRStreamArray* streams);
    int onlySetStreams(Aml_MP_DVRStreamArray* streams);
    int start(bool initialPaused);
    int stop();
    int pause();
    int resume();
    int setLimit(uint32_t time, uint32_t limit);
    int seek(int timeOffset);
    int setPlaybackRate(float rate);
    int getStatus(Aml_MP_DVRPlayerStatus* status);
    int showVideo();
    int hideVideo();
    int setVolume(float volume);
    int getVolume(float* volume);
    int setParameter(Aml_MP_PlayerParameterKey key, void* parameter);
    int getParameter(Aml_MP_PlayerParameterKey key, void* parameter);
    int setANativeWindow(ANativeWindow* nativeWindow);
    int setVideoWindow(int x, int y, int width, int height);
    int setADVolume(float volume);
    int getADVolume(float* volume);
    int getMpPlayerHandle(AML_MP_PLAYER* handle);

private:
    char mName[50];

    DVR_WrapperPlayback_t mDVRPlayerHandle{0};
    DVR_WrapperPlaybackOpenParams_t mPlaybackOpenParams{};
    DVR_PlaybackPids_t mPlayPids{};
    int mVendorID{};

    bool mIsEncryptStream;
    uint8_t* mSecureBuffer = nullptr;
    size_t mSecureBufferSize = 0;

    Aml_MP_PlayerEventCallback mEventCb = nullptr;
    void* mEventUserData = nullptr;
    //record start time
    uint32_t mRecStartTime;
    uint32_t mLimit;
    int32_t mAudioPresentationId;
    int mStartOffset;

    int setBasicParams(Aml_MP_DVRPlayerBasicParams* basicParams);
    int setDecryptParams(Aml_MP_DVRPlayerDecryptParams* decryptParams);
    DVR_Result_t eventHandlerLibDVR(DVR_PlaybackEvent_t event, void* params);
#ifdef ANDROID
    android::sp<ANativeWindow> mNativeWindow = nullptr;
    NativeWindowHelper mNativeWindowHelper;
#endif

    int createMpPlayerIfNeeded();
    int stopInternal();

    AML_MP_PLAYER mMpPlayerHandle = AML_MP_INVALID_HANDLE;
    Aml_MP_InputStreamType mInputStreamType = AML_MP_INPUT_STREAM_NORMAL;
    Aml_MP_VideoParams mVideoParams;
    Aml_MP_AudioParams mAudioParams;
    Aml_MP_AudioParams mADParams;
    Aml_MP_SubtitleParams mSubtitleParams;

    int setStreamsCommon(Aml_MP_DVRStreamArray* streams);

private:
    AmlDVRPlayer(const AmlDVRPlayer&) = delete;
    AmlDVRPlayer& operator=(const AmlDVRPlayer&) = delete;
};

}
#endif
