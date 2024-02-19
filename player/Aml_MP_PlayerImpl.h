/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_MP_PLAYER_IMPL_H_
#define _AML_MP_PLAYER_IMPL_H_

#include <Aml_MP/Aml_MP.h>
#include <utils/AmlMpRefBase.h>
#include <utils/AmlMpHandle.h>
#include <utils/AmlMpPlayerRoster.h>
#include <mutex>
#include <map>
#include "utils/AmlMpChunkFifo.h"
#include <condition_variable>
#include "cas/AmlCasBase.h"
#include "demux/AmlTsParser.h"
#ifdef ANDROID
#ifndef __ANDROID_VNDK__
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#endif

#include <system/window.h>
#include <utils/RefBase.h>
#endif

namespace aml_mp {
class AmlPlayerBase;
class AmlMpConfig;

class AmlMpPlayerImpl final : public AmlMpHandle
{
public:
    explicit AmlMpPlayerImpl(const Aml_MP_PlayerCreateParams* createParams);
    ~AmlMpPlayerImpl();
    int registerEventCallback(Aml_MP_PlayerEventCallback cb, void* userData);
    int setVideoParams(const Aml_MP_VideoParams* params);
    int setAudioParams(const Aml_MP_AudioParams* params);
    int setADParams(Aml_MP_AudioParams* params);
    int setSubtitleParams(const Aml_MP_SubtitleParams* params);
    int setCasSession(AML_MP_CASSESSION casSession);
    int setIptvCASParams(Aml_MP_CASServiceType serviceType, const Aml_MP_IptvCASParams* params);
    int start();
    int stop();
    int pause();
    int resume();
    int flush();
    int setPlaybackRate(float rate);
    int getPlaybackRate(float* rate);
    int switchAudioTrack(const Aml_MP_AudioParams* params);
    int switchSubtitleTrack(const Aml_MP_SubtitleParams* params);
    int writeData(const uint8_t* buffer, size_t size);
    int writeEsData(Aml_MP_StreamType type, const uint8_t* buffer, size_t size, int64_t pts);
    int getCurrentPts(Aml_MP_StreamType, int64_t* pts);
    int getFirstPts(Aml_MP_StreamType, int64_t* pts);
    int getBufferStat(Aml_MP_BufferStat* bufferStat);
    int setANativeWindow(ANativeWindow* nativeWindow);
    int setVideoWindow(int x, int y, int width, int height);
    int setVolume(float volume);
    int getVolume(float* volume);
    int showVideo();
    int hideVideo();
    int showSubtitle();
    int hideSubtitle();
    int setParameter(Aml_MP_PlayerParameterKey key, void* parameter);
    int getParameter(Aml_MP_PlayerParameterKey key, void* parameter);

    int setAVSyncSource(Aml_MP_AVSyncSource syncSource);
    int setPcrPid(int pid);

    int startVideoDecoding();
    int stopVideoDecoding();

    int startAudioDecoding();
    int stopAudioDecoding();

    int startADDecoding();
    int stopADDecoding();

    int startSubtitleDecoding();
    int stopSubtitleDecoding();

    int setSubtitleWindow(int x, int y, int width, int height);

    int getDecodingState(Aml_MP_StreamType streamType, AML_MP_DecodingState* streamState);

    int setADVolume(float volume);
    int getADVolume(float* volume);

private:
    enum State {
        STATE_IDLE,
        STATE_PREPARING,
        STATE_PREPARED,
        STATE_RUNNING, //audio,video,subtitle at least one is running
        STATE_PAUSED, //all paused
        STATE_STOPPED,
    };

    static const int kStreamStateBits = 2;
    static const int kStreamStateMask = 3;

    enum PrepareWaitingType {
        kPrepareWaitingNone         = 0,
        kPrepareWaitingCodecId      = 1 << 0,
        kPrepareWaitingEcm          = 1 << 1,
    };

    enum WaitingEcmMode {
        kWaitingEcmSynchronous,
        kWaitingEcmASynchronous,
    };

    struct WindowSize {
        int x = 0;
        int y = 0;
        int width = -1;
        int height = -1;
    };

    const char* stateString(State state);
    std::string decodingStateString(uint32_t streamState);
    void setState_l(State state);
    void setDecodingState_l(Aml_MP_StreamType streamType, int state);
    AML_MP_DecodingState getDecodingState_l(Aml_MP_StreamType streamType);
    int setSidebandIfNeeded_l();
    void openParser_l();
    bool tryWaitEcm_l();
    bool tryWaitCodecId_l();
    bool tryMonitorPidChange_l();
    int prepare_l();
    int finishPreparingIfNeeded_l();
    int resetIfNeeded_l(std::unique_lock<std::mutex>& lock, bool clearCasSession = true);
    int reset_l(std::unique_lock<std::mutex>& lock, bool clearCasSession);
    int applyParameters_l();
    void programEventCallback(Parser::ProgramEventType event, int param1, int param2, void* data);
    int drainDataFromBuffer_l(std::unique_lock<std::mutex>& lock);
    int doWriteData_l(const uint8_t* buffer, size_t size, std::unique_lock<std::mutex>& lock);

    void notifyListener(Aml_MP_PlayerEventType eventType, int64_t param);

    int start_l();
    int stop_l(std::unique_lock<std::mutex>& lock, bool clearCasSession = true);
    int pause_l();
    int resume_l();

    int startVideoDecoding_l();
    int startAudioDecoding_l();
    int startSubtitleDecoding_l();
    int startADDecoding_l();
    int stopAudioDecoding_l(std::unique_lock<std::mutex>& lock);
    int stopSubtitleDecoding_l(std::unique_lock<std::mutex>& lock);
    int stopADDecoding_l(std::unique_lock<std::mutex>& lock);

    int startDescrambling_l();
    int stopDescrambling_l();

    int switchDecodeMode_l(Aml_MP_VideoDecodeMode decodeMode, std::unique_lock<std::mutex>& lock);

    int resetADCodec_l(bool callStart);
    int resetAudioCodec_l(bool callStart);

    int setAudioParams_l(const Aml_MP_AudioParams* params);
    int setSubtitleParams_l(const Aml_MP_SubtitleParams* params);
    int setParameter_l(Aml_MP_PlayerParameterKey key, void* parameter, std::unique_lock<std::mutex>& lock);

    int enableAFD_l(bool enable);
    int setVideoAFDAspectMode_l(Aml_MP_VideoAFDAspectMode aspectMode);

    void statisticWriteDataRate_l(size_t size);
    void collectBuffingInfos_l();

    void resetVariables_l();
    void resetDrmVariables_l();

    void increaseDmxSecMemSize();
    void recoverDmxSecMemSize();

    const int mInstanceId;
    char mName[50];

    std::mutex mEventLock;
    Aml_MP_PlayerEventCallback mEventCb = nullptr;
    void* mUserData = nullptr;
    std::atomic<pid_t> mEventCbTid{-1};


    mutable std::mutex mLock;
    State mLastState{STATE_IDLE};
    State mState{STATE_IDLE};
    uint32_t mStreamState{0};
    uint32_t mPrepareWaitingType{kPrepareWaitingNone};
    WaitingEcmMode mWaitingEcmMode = kWaitingEcmSynchronous;
    WaitingEcmMode mUserWaitingEcmMode = kWaitingEcmSynchronous;
    bool mFirstEcmWritten = false;
    bool mAudioStoppedInSwitching = false;

    Aml_MP_PlayerCreateParams mCreateParams;

    Aml_MP_VideoParams mVideoParams;
    Aml_MP_AudioParams mAudioParams;
    Aml_MP_SubtitleParams mSubtitleParams;
    AML_MP_TeletextCtrlParam mTeletextCtrlParam;
    WindowSize mSubtitleWindow;
    Aml_MP_AudioParams mADParams;

    std::vector<int> mEcmPids;

    Aml_MP_VideoDecodeMode mVideoDecodeMode{AML_MP_VIDEO_DECODE_MODE_NONE};

    Aml_MP_VideoDisplayMode mVideoDisplayMode{(Aml_MP_VideoDisplayMode)-1};
    int mBlackOut{-1};
    int mVideoPtsOffset{-1};
    Aml_MP_AudioOutputMode mAudioOutputMode{(Aml_MP_AudioOutputMode)-1};
    Aml_MP_AudioOutputDevice mAudioOutputDevice{(Aml_MP_AudioOutputDevice)-1};
    int mAudioPtsOffset{-1};
    Aml_MP_AudioBalance mAudioBalance{(Aml_MP_AudioBalance)-1};
    int mAudioMute{-1};
    int mNetworkJitter{-1};
    Aml_MP_ADVolume mADMixLevel{-1, -1};
    Aml_MP_PlayerWorkMode mWorkMode{(Aml_MP_PlayerWorkMode)-1};
    Aml_MP_VideoErrorRecoveryMode mVideoErrorRecoveryMode{(Aml_MP_VideoErrorRecoveryMode)-1};
    Aml_MP_AudioLanguage mAudioLanguage;
    int mAudioBlockAlign{-1};

    float mVolume = -1.0;
    float mADVolume = -1.0;

    float mPlaybackRate = 1.0f;
    NativeWindowHelper mNativeWindowHelper;
    WindowSize mVideoWindow;
    int mVideoTunnelId = -1;
    void* mSurfaceHandle = nullptr;
    int mAudioPresentationId = -1;
    int mUseTif = -1;
    int mSPDIFStatus = -1;
    Aml_MP_Rect mVideoCrop{0, 0, -1, -1};

    Aml_MP_AVSyncSource mSyncSource = AML_MP_AVSYNC_SOURCE_DEFAULT;
    int mPcrPid = AML_MP_INVALID_PID;

    sptr<AmlPlayerBase> mPlayer;

    Aml_MP_CASServiceType mCasServiceType{AML_MP_CAS_SERVICE_TYPE_INVALID};
    Aml_MP_IptvCASParams mIptvCasParams;

    bool mIsStandaloneCas = false;
    sptr<AmlCasBase> mCasHandle;

    static constexpr int kZorderBase = -2;
    int mZorder;
#ifdef ANDROID
    android::sp<ANativeWindow> mNativeWindow;
#ifndef __ANDROID_VNDK__
    android::sp<android::SurfaceComposerClient> mComposerClient;
    android::sp<android::SurfaceControl> mSurfaceControl;
    android::sp<android::Surface> mSurface = nullptr;
#endif
#endif

    sptr<Parser> mParser;
    AmlMpChunkFifo mTsBuffer;
    sptr<AmlMpBuffer> mWriteBuffer;

    int64_t mLastBytesWritten = 0;
    int64_t mLastWrittenTimeUs = 0;
    bool mVideoShowState = true;

    Aml_MP_VideoAFDAspectMode mVideoAFDAspectMode = AML_MP_VIDEO_AFD_ASPECT_MODE_NONE;

private:
    AmlMpPlayerImpl(const AmlMpPlayerImpl&) = delete;
    AmlMpPlayerImpl& operator= (const AmlMpPlayerImpl&) = delete;
};

}


#endif
