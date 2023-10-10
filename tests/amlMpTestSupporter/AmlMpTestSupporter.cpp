/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpPlayerDemo_TestSupporter"
#include <utils/AmlMpLog.h>
#include "AmlMpTestSupporter.h"
#include "TestUtils.h"
#include <Aml_MP/Aml_MP.h>
#include "source/Source.h"
#include "demux/AmlTsParser.h"
#include "ParserReceiver.h"
#include "Playback.h"
#include "DVRRecord.h"
#include "DVRPlayback.h"
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <Aml_MP/Dvr.h>

static const char* mName = LOG_TAG;

//0x0606 used for irdeto 1
//0x0602 0x0604 0x0606 0x0608 0x0622 0x0626 0x0664 0x0614 used for irdeto 2
//0x0692 used for irdeto 3
//0x0625 is used for DVT test stream
static const uint16_t irdetoCasIdList[] = {0x0602,0x0604,0x0606,0x0608,0x0622,0x0626,0x0664,0x0614,0x0692,0x0625};

static bool isIrdetoCas(uint16_t cas_id)
{
    int size = sizeof(irdetoCasIdList) / sizeof(irdetoCasIdList[0]);

    for (int idx = 0; idx < size; idx++)
    {
        if (irdetoCasIdList[idx] == cas_id)
            return true;
    }

    return false;
}

namespace aml_mp {
AmlMpTestSupporter::AmlMpTestSupporter()
{
    ALOGI("AmlMpTestSupporter structure\n");
}

AmlMpTestSupporter::~AmlMpTestSupporter()
{
    MLOGI("%s", __FUNCTION__);
}

void AmlMpTestSupporter::playerRegisterEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    mEventCallback = cb;
    mUserData = userData;
}

void AmlMpTestSupporter::DVRRecorderRegisterEventCallback(Aml_MP_DVRRecorderEventCallback cb, void* userData)
{
    mDVRRecorderEventCallback = cb;
    mUserData = userData;
}

sptr<NativeUI> AmlMpTestSupporter::createNativeUI()
{
    if (mNativeUI == nullptr)
    {
         mNativeUI = new NativeUI();
    }

    if (mDisplayParam.width < 0)
    {
        mDisplayParam.width = mNativeUI->getDefaultSurfaceWidth();
    }

    if (mDisplayParam.height < 0)
    {
        mDisplayParam.height = mNativeUI->getDefaultSurfaceHeight();
    }
    return mNativeUI;
}

#ifdef ANDROID
sp<ANativeWindow> AmlMpTestSupporter::getSurfaceControl()
{
    mNativeUI->controlSurface(
            mDisplayParam.x,
            mDisplayParam.y,
            mDisplayParam.x + mDisplayParam.width,
            mDisplayParam.y + mDisplayParam.height);
    mNativeUI->controlSurface(mDisplayParam.zorder);
    sp<ANativeWindow> window = mNativeUI->getNativeWindow();
    return window;
}

void AmlMpTestSupporter::setWindow(bool mSurface)
{
    if (mSurface) {
        createNativeUI();
        sp<ANativeWindow> window = getSurfaceControl();
        mPlayback->setANativeWindow(window);
    } else {
        mPlayback->setANativeWindow(nullptr);
    }
}
#endif

int AmlMpTestSupporter::setDataSource(const std::string& url)
{
    mUrl = url;
    return 0;
}

sptr<ProgramInfo> AmlMpTestSupporter::getProgramInfo() const
{
    return mProgramInfo;
}

int AmlMpTestSupporter::prepare(bool cryptoMode)
{
    int ret = 0;

    mCryptoMode = cryptoMode;
    MLOGI("mCryptoMode: %d \n", mCryptoMode);

    mSource = Source::create(mUrl.c_str());
    if (mSource == nullptr) {
        MLOGE("create source failed!");
        return -1;
    }

    ret = mSource->initCheck();
    if (ret < 0) {
        MLOGE("source initCheck failed!");
        return -1;
    }

    mDemuxId = mSource->getDemuxId();
    Aml_MP_DemuxId demuxId = mDemuxId;
    int programNumber = mSource->getProgramNumber();
    Aml_MP_DemuxSource sourceId = mSource->getSourceId();
    Aml_MP_Initialize();

    Aml_MP_DemuxType demuxType = AML_MP_DEMUX_TYPE_HARDWARE;
    if (mOptions & AML_MP_OPTION_PREFER_TUNER_HAL) {
        demuxType = AML_MP_DEMUX_TYPE_TUNERHAL;
    } else if(mSource->getFlags()&Source::kIsHardwareSource) {
        demuxType = AML_MP_DEMUX_TYPE_HARDWARE;
    } else {
        demuxType = AML_MP_DEMUX_TYPE_SOFTWARE;
    }

    //set default demux source
    if (mSource->getFlags()&Source::kIsHardwareSource) {
        Aml_MP_SetDemuxSource(demuxId, sourceId);
    } else {
        if (sourceId < AML_MP_DEMUX_SOURCE_DMA0) {
            sourceId = Aml_MP_DemuxSource(sourceId + AML_MP_DEMUX_SOURCE_DMA0);
            MLOGW("change source id to %d", sourceId);
        }
        Aml_MP_SetDemuxSource(demuxId, sourceId);
    }

    if (mSource->getFlags()&Source::kIsDVRSource) {
        mIsDVRPlayback = true;
        MLOGI("dvr playback");
        return 0;
    }

    mParser = new Parser(demuxId, mSource->getFlags()&Source::kIsHardwareSource, demuxType);
    if (mParser == nullptr) {
        MLOGE("create parser failed!");
        return -1;
    }
    mParser->selectProgram(programNumber);
    mParser->setEventCallback([this] (Parser::ProgramEventType event, int param1, int param2, void* data) {
            return programEventCallback(event, param1, param2, data);
    });
    ret = mParser->open();
    if (ret < 0) {
        MLOGE("parser open failed!");
        return -1;
    }
    mParser->parseProgramInfoAsync();

    mParserReceiver = new ParserReceiver(mParser);
    mSource->addSourceReceiver(mParserReceiver);

    ret = mSource->start();
    if (ret < 0) {
        MLOGE("source start failed!");
        return -1;
    }

    MLOGI("parsing...");
    ret = mParser->waitProgramInfoParsed();
    if (ret < 0) {
        MLOGE("parser wait program info failed!");
        return -1;
    }

    mProgramInfo = mParser->getProgramInfo();
    if (mProgramInfo == nullptr) {
        MLOGE("get programInfo failed!");
        return -1;
    }

    if (mProgramInfo->scrambled && isIrdetoCas(mProgramInfo->caSystemId)) {
        ret = mParser->waitCATSectionDataParsed();
        if (ret < 0) {
            MLOGE("parser wait CAT failed!");
            return -1;
        }
    }

    MLOGI("parsed done!");
    mSource->removeSourceReceiver(mParserReceiver);
    mSource->restart();
    mParser->close();

    return 0;
}

int AmlMpTestSupporter::setParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    int ret = 0;
    if (mPlayback != nullptr) {
        ret = mPlayback->setParameter(key, parameter);
    }
    return ret;
}

int AmlMpTestSupporter::setAVSyncSource(Aml_MP_AVSyncSource syncSource)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);
    mSyncSource = syncSource;

    return 0;
}

int AmlMpTestSupporter::setPcrPid(int pid)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);
    mPcrPid = pid;

    return 0;
}

void AmlMpTestSupporter::setVideoErrorRecoveryMode(int videoErrorRecoveryMode)
{
    mpVideoErrorRecoveryMode = videoErrorRecoveryMode;
    MLOGI("mpVideoErrorRecoveryMode: %d\n", mpVideoErrorRecoveryMode);
}

void AmlMpTestSupporter::setSourceMode(bool esMode, bool clearTVP)
{
    mEsMode = esMode;
    mClearTVP = clearTVP;
    MLOGI("mEsMode:%d, mClearTVP:%d", mEsMode, mClearTVP);
}

int AmlMpTestSupporter::startPlay(PlayMode playMode, bool mStart, bool mSourceReceiver, bool isTimeShift)
{
    int ret = 0;
    mPlayMode = playMode;
    ALOGI(">>>> AmlMpTestSupporter startPlay\n");
    if (mIsDVRPlayback) {
        return startDVRPlayback(true, isTimeShift);
    }
    mDemuxId = mParser->getDemuxId();
    Aml_MP_DemuxId demuxId = mDemuxId;
    Aml_MP_InputSourceType sourceType = AML_MP_INPUT_SOURCE_TS_MEMORY;
    Aml_MP_InputStreamType inputStreamType{AML_MP_INPUT_STREAM_NORMAL};
    AML_MP_CASSESSION casSession = AML_MP_INVALID_HANDLE;

    if (mSource->getFlags() & Source::kIsHardwareSource) {
        sourceType = AML_MP_INPUT_SOURCE_TS_DEMOD;
    }

    if (mEsMode) {
        sourceType = AML_MP_INPUT_SOURCE_ES_MEMORY;
        mSyncSource = AML_MP_AVSYNC_SOURCE_AUDIO;
    }

    if (mProgramInfo->scrambled && mCasPlugin == nullptr) {
        mCasPlugin = new CasPlugin(demuxId, sourceType, mProgramInfo);
        mCasPlugin->setCATSectionData(mCATSection);
        mCasPlugin->setPMTSectionData(mPMTSection);
        if (mCasPlugin->start() < 0) {
            MLOGE("CasPlugin start failed!");
        } else {
            casSession = mCasPlugin->casSession();
            inputStreamType = mCasPlugin->inputStreamType();
        }
    }

    if (mClearTVP) {
        inputStreamType = AML_MP_INPUT_STREAM_SECURE_MEMORY;
    }

    if (mPlayback == nullptr) {
        MLOGI("sourceType:%d, inputStreamType:%d\n", sourceType, inputStreamType);
        mTestModule = mPlayback = new Playback(demuxId, sourceType, inputStreamType, mOptions);
    }

    if (mEventCallback != nullptr) {
        mPlayback->playerRegisterEventCallback(mEventCallback, mUserData);
    }
    createNativeUI();

    ret = mPlayback->setSubtitleDisplayWindow(mDisplayParam.width, 0, mDisplayParam.width, mDisplayParam.height);

#ifdef ANDROID
#ifndef __ANDROID_VNDK__
    if (mWorkMode == AML_MP_PLAYER_MODE_NORMAL) {
        if (!mDisplayParam.videoMode) {
            if (mDisplayParam.aNativeWindow) {
                mPlayback->setANativeWindow(mDisplayParam.aNativeWindow);
            } else {
                sp<ANativeWindow> window = getSurfaceControl();
                if (window == nullptr) {
                    MLOGE("create native window failed!");
                    return -1;
                }
                mPlayback->setANativeWindow(window);
            }
        } else {
            setOsdBlank(1);
            mPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_WINDOW_ZORDER, &mDisplayParam.zorder);
            mPlayback->setVideoWindow(mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
            MLOGI("x:%d y:%d width:%d height:%d\n", mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
        }
    }
#else
    if (mDisplayParam.channelId < 0) {
        printf("Please specify the ui channel id by --id option, default set to 0 now!\n");
        mDisplayParam.channelId = 0;
    }
    mPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID, &mDisplayParam.channelId);
#endif
#else
    if (mDisplayParam.channelId < 0) {
        printf("Please specify the ui channel id by --id option, default set to 0 now!\n");
        mDisplayParam.channelId = 0;
    }
    mPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID, &mDisplayParam.channelId);
    mPlayback->setVideoWindow(mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
    MLOGI("x:%d y:%d width:%d height:%d\n", mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
#endif
    if (mStart == true)
    {
        mPlayback->setAVSyncSource(mSyncSource);
        mPlayback->setParameter(AML_MP_PLAYER_PARAMETER_WORK_MODE, &mWorkMode);
        mPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_ERROR_RECOVERY_MODE, &mpVideoErrorRecoveryMode);
        if (mSyncSource == AML_MP_AVSYNC_SOURCE_PCR && mPcrPid != AML_MP_INVALID_PID)
        {
            mPlayback->setPcrPid(mPcrPid);
        }
        ret = mPlayback->start(mProgramInfo, casSession, mPlayMode);
        if (ret < 0)
        {
            MLOGE("playback start failed!");
            return -1;
        }
    }else {
        ALOGI("<<<<AmlMpTestSupporter haven't set A/V params and startdecoding\n");
    }
    if (mSourceReceiver == true)
    {
        mSource->addSourceReceiver(mPlayback);
    }else {
        ALOGI("<<<<AmlMpTestSupporter haven't addSourceReceiver\n");
    }
    ALOGI("<<<< AmlMpTestSupporter startPlay\n");
    return 0;
}

int AmlMpTestSupporter::startRecord(bool isSetStreams, bool isTimeShift, bool appendMode)
{
    int ret = 0;
    ALOGI("enter startRecord\n");
    mDemuxId = mParser->getDemuxId();
    Aml_MP_DemuxId demuxId = mDemuxId;

    mTestModule = mRecorder = new DVRRecord(mCryptoMode, demuxId, mProgramInfo, isTimeShift, appendMode);
    ALOGI("before mRecorder start\n");

    if (mDVRRecorderEventCallback != nullptr) {
        mRecorder->DVRRecorderRegisterEventCallback(mDVRRecorderEventCallback, mUserData);
    }

    if (isSetStreams) {
        ret = mRecorder->start(isSetStreams);
    }

    if (ret < 0) {
        MLOGE("start recorder failed!");
        return -1;
    }

    mSource->addSourceReceiver(mRecorder);
    return 0;
}

int AmlMpTestSupporter::DVRRecorder_setStreams()
{
    int ret = 0;
    if (mRecorder == nullptr) return -1;

    ret = mRecorder->setStreams();
    if (ret < 0) {
        MLOGE("setStreams failed!");
        return -1;
    }
    return 0;
}

int AmlMpTestSupporter::DVRPlayback_setStreams()
{
    int ret = 0;
    if (mDVRPlayback == nullptr) return -1;

    ret = mDVRPlayback->setStreams();
    if (ret < 0) {
        MLOGE("setStreams failed!");
        return -1;
    }
    return 0;
}

int AmlMpTestSupporter::startDVRRecorderAfterSetStreams()
{
    int ret = 0;
    ret = mRecorder->start(false);
    if (ret < 0) {
        MLOGE("DVRRecorder setStreams: start failed!");
        return -1;
    }
    return 0;
}

int AmlMpTestSupporter::startDVRPlaybackAfterSetStreams()
{
    int ret = 0;
    ret = mDVRPlayback->start(false);
    if (ret < 0) {
        MLOGE("DVRPlayback setStreams: start failed!");
        return -1;
    }
    return 0;
}

std::string AmlMpTestSupporter::stripUrlIfNeeded(const std::string& url) const
{
    printf("url: %s \n", url.c_str());
    std::string result = url;

    auto suffix = result.rfind(".ts");
    if (suffix != result.npos) {
        auto hyphen = result.find_last_of('-', suffix);
        if (hyphen != result.npos) {
            result = result.substr(0, hyphen);
        }
    }
    printf("result: %s \n", result.c_str());
    result.erase(0, strlen("dvr://"));
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

int AmlMpTestSupporter::startDVRPlayback(bool isSetStreams, bool isTimeShift)
{
    MLOG();
    int ret = 0;

    mDemuxId = mSource->getDemuxId();
    Aml_MP_DemuxId demuxId = mDemuxId;
    mTestModule = mDVRPlayback = new DVRPlayback(mUrl, mCryptoMode, demuxId, isTimeShift);

    if (mEventCallback != nullptr) {
        mDVRPlayback->registerEventCallback(mEventCallback, mUserData);
    }

    createNativeUI();

#ifdef ANDROID
#ifndef __ANDROID_VNDK__
    sp<ANativeWindow> window = getSurfaceControl();
    if (window == nullptr) {
        MLOGE("create native window failed!");
        return -1;
    }
    mDVRPlayback->setANativeWindow(window);
#else
    if (mDisplayParam.channelId < 0) {
        printf("Please specify the ui channel id by --id option!\n");
        return -1;
    }

    mDVRPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID, &mDisplayParam.channelId);
#endif
#else
    if (mDisplayParam.channelId < 0) {
        printf("Please specify the ui channel id by --id option, default set to 0 now!\n");
        mDisplayParam.channelId = 0;
    }
    mDVRPlayback->setParameter(AML_MP_PLAYER_PARAMETER_VIDEO_TUNNEL_ID, &mDisplayParam.channelId);
    mDVRPlayback->setVideoWindow(mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
    MLOGI("x:%d y:%d width:%d height:%d\n", mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height);
#endif

    if (isSetStreams) {
        ret = mDVRPlayback->start();
    }
    if (ret < 0) {
        MLOGE("DVR playback start failed!");
        return -1;
    }

    return 0;
}

int AmlMpTestSupporter::startUIOnly()
{
    int ret = 0;
#ifdef ANDROID
    mNativeUI = new NativeUI();
    if (mDisplayParam.width < 0) {
        mDisplayParam.width = mNativeUI->getDefaultSurfaceWidth();
    }

    if (mDisplayParam.height < 0) {
        mDisplayParam.height = mNativeUI->getDefaultSurfaceHeight();
    }

    mNativeUI->controlSurface(
            mDisplayParam.x,
            mDisplayParam.y,
            mDisplayParam.x + mDisplayParam.width,
            mDisplayParam.y + mDisplayParam.height);
    mNativeUI->controlSurface(mDisplayParam.zorder);
    sp<ANativeWindow> window = mNativeUI->getNativeWindow();
    if (window == nullptr) {
        MLOGE("create native window failed!");
        return -1;
    }

    int videoTunnelId = -1;
    ret = mNativeWindowHelper.setSidebandNonTunnelMode(window.get(), &videoTunnelId);
    printf("created ui channel id:%d\n", videoTunnelId);

#endif

    return ret;
}

int AmlMpTestSupporter::stop()
{
    MLOGI("stopping...");

    if (mSource != nullptr) {
        mSource->stop();
        mSource.clear();
    }

    if (mParser != nullptr) {
        mParser->close();
        mParser.clear();
    }

    if (mPlayback != nullptr) {
        mPlayback->stop();
    }

    if (mRecorder != nullptr) {
        mRecorder->stop();
    }

    if (mDVRPlayback != nullptr) {
        mDVRPlayback->stop();
    }

    if (mCasPlugin != nullptr) {
        mCasPlugin->stop();
        mCasPlugin.clear();
    }

    if (mDisplayParam.videoMode) {
        setOsdBlank(0);
    }

    MLOGI("stop end!");
    return 0;
}

bool AmlMpTestSupporter::hasVideo() const
{
    if (mProgramInfo) {
        return mProgramInfo->videoCodec != AML_MP_CODEC_UNKNOWN && mProgramInfo->videoPid != AML_MP_INVALID_PID;
    } else {
        return false;
    }
}

int AmlMpTestSupporter::installSignalHandler()
{
    int ret = 0;
    sigset_t blockSet, oldMask;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGINT);
    ret = pthread_sigmask(SIG_SETMASK, &blockSet, &oldMask);
    if (ret != 0) {
        MLOGE("pthread_sigmask failed! %d", ret);
        return -1;
    }

    mSignalHandleThread = std::thread([blockSet, oldMask, this] {
        int signo;
        int err;

        for (;;) {
            err = sigwait(&blockSet, &signo);
            if (err != 0) {
                MLOGE("sigwait error! %d", err);
                exit(0);
            }

            printf("%s\n", strsignal(signo));

            switch (signo) {
            case SIGINT:
            {
                signalQuit();
            }
            break;

            default:
                break;
            }

            if (pthread_sigmask(SIG_SETMASK, &oldMask, nullptr) != 0) {
                MLOGE("restore sigmask failed!");
            }
        }
    });
    mSignalHandleThread.detach();

    return ret;
}

int AmlMpTestSupporter::fetchAndProcessCommands()
{
    char promptBuf[50];
    snprintf(promptBuf, sizeof(promptBuf), "AmlMpTestSupporter >");

    mCommandProcessor = new CommandProcessor(promptBuf);
    if (mCommandProcessor == nullptr) {
        return -1;
    }

    mCommandProcessor->setCommandVisitor(std::bind(&AmlMpTestSupporter::processCommand, this, std::placeholders::_1));
    mCommandProcessor->setInterrupter([this]() {return mQuitPending;});
    return mCommandProcessor->fetchAndProcessCommands();
}

bool AmlMpTestSupporter::processCommand(const std::vector<std::string>& args)
{
    bool ret = true;
    std::string cmd = *args.begin();

    if (cmd == "osd") {
    #ifdef ANDROID
        mNativeUI->controlSurface(-2);
    #endif
    } else if (cmd == "video") {
    #ifdef ANDROID
        mNativeUI->controlSurface(0);
    #endif
    } else if (cmd == "zorder") {
    #ifdef ANDROID
        if (args.size() == 2) {
            int zorder = std::stoi(args[1]);
            mNativeUI->controlSurface(zorder);
        }
    #endif
    } else if (cmd == "resize") {
        int x = -1;
        int y = -1;
        int width = -1;
        int height = -1;
        if (args.size() == 5) {
            x = std::stoi(args[1]);
            y = std::stoi(args[2]);
            width = std::stoi(args[3]);
            height = std::stoi(args[4]);
            mNativeUI->controlSurface(x, y, width, height);
        }
    } else {
        mTestModule->processCommand(args);
    }

    return ret;
}

void AmlMpTestSupporter::signalQuit()
{
    MLOGI("received SIGINT, %s", __FUNCTION__);

    mQuitPending = true;

    if (mSource) mSource->signalQuit();
    if (mParser) mParser->signalQuit();
    if (mPlayback) mPlayback->signalQuit();
}

void AmlMpTestSupporter::programEventCallback(Parser::ProgramEventType event, int param1, int param2, void* data)
{

    switch (event)
    {
        case Parser::ProgramEventType::EVENT_PMT_PARSED:
        {
            MLOGI("received program PMT parsed event");
            if (mPMTSection == nullptr) {
                mPMTSection = new SectionData();
            }
            mPMTSection->reset();
            mPMTSection->init(param1, param2, (uint8_t *)data);
            MLOGI("received program PMT data %p,size %d", mPMTSection->data, mPMTSection->size);
            break;
        }
        case Parser::ProgramEventType::EVENT_CAT_PARSED:
        {
            MLOGI("received program CAT parsed event");
            if (mCATSection == nullptr) {
                mCATSection = new SectionData();
            }
            mCATSection->reset();
            mCATSection->init(param1, param2, (uint8_t *)data);
            MLOGI("received program CAT data %p,size %d", mCATSection->data, mCATSection->size);
            break;
        }
        case Parser::ProgramEventType::EVENT_PROGRAM_PARSED:
        {
            MLOGI("received program parsed event");
            break;
        }
        case Parser::ProgramEventType::EVENT_AV_PID_CHANGED:
        {
            MLOGI("received program av pid changed event");
            break;
        }
        case Parser::ProgramEventType::EVENT_ECM_DATA_PARSED:
        {
            MLOGI("received program ecm data parsed event");
            break;
        }
    }
}

void AmlMpTestSupporter::setDisplayParam(const DisplayParam& param)
{
    mDisplayParam = param;
    MLOGI("x:%d, y:%d, width:%d, height:%d, zorder:%d, videoMode:%d\n",
            mDisplayParam.x, mDisplayParam.y, mDisplayParam.width, mDisplayParam.height, mDisplayParam.zorder, mDisplayParam.videoMode);
}

void AmlMpTestSupporter::addOptions(uint64_t options) {
    mOptions |= options;
}

int AmlMpTestSupporter::setOsdBlank(int blank)
{
    auto writeNode = [] (const char *path, int value) -> int {
        int fd;
        char cmd[128] = {0};
        fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (fd >= 0)
        {
            sprintf(cmd,"%d",value);
            write (fd,cmd,strlen(cmd));
            close(fd);
            return 0;
        }
        return -1;
    };
    MLOGI("setOsdBlank: %d", blank);
    int ret = 0;
    #if ANDROID_PLATFORM_SDK_VERSION >= 30
        ret += writeNode("/sys/kernel/debug/dri/0/vpu/blank", blank);
    #else
        ret += writeNode("/sys/class/graphics/fb0/osd_display_debug", blank);
        ret += writeNode("/sys/class/graphics/fb0/blank", blank);
    #endif
    return ret;
}

sptr<TestModule> AmlMpTestSupporter::getPlayback() const
{
    return mPlayback;
}

//getRecorder
sptr<TestModule> AmlMpTestSupporter::getRecorder() const
{
    return mRecorder;
}

//getDVRPlayer
sptr<TestModule> AmlMpTestSupporter::getDVRPlayer() const
{
    return mDVRPlayback;
}
}
