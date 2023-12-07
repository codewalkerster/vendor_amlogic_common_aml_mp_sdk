/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlMpPlayerDemo_Playback"
#include <utils/AmlMpLog.h>
#include "Playback.h"
#include <Aml_MP/Aml_MP.h>
#include <utils/AmlMpUtils.h>
#include <cutils/properties.h>
#include <vector>
#include <string>
#include <fcntl.h>

static const char* mName = LOG_TAG;

namespace aml_mp {
////////////////////////////////////////////////////////////////////////////////
CasPlugin::CasPlugin(Aml_MP_DemuxId demuxId, Aml_MP_InputSourceType sourceType, const sptr<ProgramInfo>& programInfo)
: mDemuxId(demuxId)
, mSourceType(sourceType)
, mProgramInfo(programInfo)
{
    Aml_MP_CAS_Initialize();
}

CasPlugin::~CasPlugin()
{
    Aml_MP_CAS_Terminate();
}

int CasPlugin::start()
{
    if (mSourceType == AML_MP_INPUT_SOURCE_TS_DEMOD) {
        return startDVBDescrambling();
    } else if (mSourceType == AML_MP_INPUT_SOURCE_TS_MEMORY) {
        return startIPTVDescrambling();
    }

    return -1;
}

int CasPlugin::stop()
{
    if (mSourceType == AML_MP_INPUT_SOURCE_TS_DEMOD) {
        return stopDVBDescrambling();
    } else if (mSourceType == AML_MP_INPUT_SOURCE_TS_MEMORY) {
        return stopIPTVDescrambling();
    }

    return -1;
}

void CasPlugin::setPMTSectionData(const sptr<SectionData>& pmt)
{
    if (pmt == nullptr || pmt->size == 0 || pmt->data == nullptr) {
        return;
    }

    if (Aml_MP_CAS_IsNeedWholeSection())
    {
        Aml_MP_CASSectionReportAttr attr;

        memset(&attr, 0, sizeof(attr));
        attr.dmxDev = mDemuxId;
        // attr.serviceId;
        attr.sectionType = AML_MP_CAS_SECTION_PMT;
        Aml_MP_CAS_ReportSection(&attr, pmt->data, pmt->size);
    }
}

void CasPlugin::setCATSectionData(const sptr<SectionData>& cat)
{
    if (cat == nullptr || cat->size == 0 || cat->data == nullptr) {
        return;
    }

    if (Aml_MP_CAS_IsNeedWholeSection())
    {
        Aml_MP_CASSectionReportAttr attr;

        memset(&attr, 0, sizeof(attr));
        attr.dmxDev = mDemuxId;
        // attr.serviceId;
        attr.sectionType = AML_MP_CAS_SECTION_CAT;
        Aml_MP_CAS_ReportSection(&attr, cat->data, cat->size);
    }
}

int CasPlugin::startDVBDescrambling()
{
    MLOG();

    Aml_MP_CAS_SetEmmPid(mDemuxId, mProgramInfo->emmPid);

    if (!Aml_MP_CAS_IsSystemIdSupported(mProgramInfo->caSystemId)) {
        MLOGE("unsupported caSystemId:%#x", mProgramInfo->caSystemId);
        return -1;
    }

    int ret = Aml_MP_CAS_OpenSession(&mCasSession, AML_MP_CAS_SERVICE_LIVE_PLAY);
    if (ret < 0) {
        MLOGE("open session failed!");
        return -1;
    }


    Aml_MP_CAS_RegisterEventCallback(mCasSession, [](AML_MP_CASSESSION session __unused, const char* json) {
        MLOGI("ca_cb:%s", json);
        return 0;
    }, this);

    Aml_MP_CASServiceInfo casServiceInfo;
    memset(&casServiceInfo, 0, sizeof(casServiceInfo));
    casServiceInfo.service_id = mProgramInfo->serviceNum;
    casServiceInfo.serviceType = AML_MP_CAS_SERVICE_LIVE_PLAY;
    casServiceInfo.ecm_pid = mProgramInfo->ecmPid[0];
    casServiceInfo.stream_pids[0] = mProgramInfo->audioPid;
    casServiceInfo.stream_pids[1] = mProgramInfo->videoPid;
    casServiceInfo.stream_num = 2;
    casServiceInfo.ca_private_data_len = 0;
    ret = Aml_MP_CAS_StartDescrambling(mCasSession, &casServiceInfo);
    if (ret < 0) {
        MLOGE("start descrambling failed with %d", ret);
        return ret;
    }

    mSecMem = Aml_MP_CAS_CreateSecmem(mCasSession, AML_MP_CAS_SERVICE_LIVE_PLAY, nullptr, nullptr);
    if (mSecMem == nullptr) {
        MLOGE("create secmem failed!");
    }

    return 0;
}

int CasPlugin::stopDVBDescrambling()
{
    MLOG("mCasSession:%p", mCasSession);

    if (mCasSession == nullptr) {
        return 0;
    }

    Aml_MP_CAS_DestroySecmem(mCasSession, mSecMem);
    mSecMem = nullptr;

    Aml_MP_CAS_StopDescrambling(mCasSession);

    Aml_MP_CAS_CloseSession(mCasSession);
    mCasSession = nullptr;

    return 0;
}

int CasPlugin::startIPTVDescrambling()
{
    MLOG();

    Aml_MP_CASServiceType serviceType{AML_MP_CAS_SERVICE_TYPE_INVALID};
    Aml_MP_IptvCASParams casParams;
    int ret = 0;

    switch (mProgramInfo->caSystemId) {
    case 0x5601:
    {
        MLOGI("verimatrix iptv!");
        serviceType = AML_MP_CAS_SERVICE_VERIMATRIX_IPTV;

        casParams.videoCodec = mProgramInfo->videoCodec;
        casParams.audioCodec = mProgramInfo->audioCodec;
        casParams.videoPid = mProgramInfo->videoPid;
        casParams.audioPid = mProgramInfo->audioPid;
        casParams.ecmPid[0] = mProgramInfo->ecmPid[0];
        casParams.ecmPid[1] = mProgramInfo->ecmPid[1];
        casParams.demuxId = mDemuxId;

        char value[PROPERTY_VALUE_MAX];
        property_get("media.vmx.storepath", value, "/data/mediadrm");
        strncpy(casParams.keyPath, value, sizeof(casParams.keyPath)-1);

        property_get("media.vmx.serveraddr", value, "client-test-3.verimatrix.com");
        strncpy(casParams.serverAddress, value, sizeof(casParams.serverAddress)-1);

        casParams.serverPort = property_get_int32("media.vmx.port", 12686);
    }
    break;

    case 0x1724:
    {
        MLOGI("VMX DVB");
    }
    break;

    case 0x4AD4:
    case 0x4AD5:
    {
        MLOGI("wvcas iptv, systemid=0x%x!", mProgramInfo->caSystemId);
        serviceType = AML_MP_CAS_SERVICE_WIDEVINE;

        casParams.caSystemId = mProgramInfo->caSystemId;
        casParams.videoCodec = mProgramInfo->videoCodec;
        casParams.audioCodec = mProgramInfo->audioCodec;
        casParams.videoPid = mProgramInfo->videoPid;
        casParams.audioPid = mProgramInfo->audioPid;
        casParams.ecmPid[0] = mProgramInfo->ecmPid[0];
        casParams.ecmPid[1] = mProgramInfo->ecmPid[1];
        casParams.demuxId = mDemuxId;

        casParams.private_size = 0;
        if (mProgramInfo->privateDataLength > 0) {
            memcpy(casParams.private_data, mProgramInfo->privateData, mProgramInfo->privateDataLength);
            casParams.private_size =  mProgramInfo->privateDataLength;
        }
        MLOGI("wvcas iptv, private_size=%zu", casParams.private_size);
    }
    break;

    default:
        MLOGI("unknown caSystemId:%#x", mProgramInfo->caSystemId);
        break;
    }

    if (serviceType != AML_MP_CAS_SERVICE_TYPE_INVALID) {
        ret = Aml_MP_CAS_OpenSession(&mCasSession, serviceType);
        ret += Aml_MP_CAS_StartDescramblingIPTV(mCasSession, &casParams);
        if (ret < 0) {
            MLOGE("start iptv cas session failed!");
        }
    }

    return ret;
}

int CasPlugin::stopIPTVDescrambling()
{
    int ret = 0;

    ret = Aml_MP_CAS_StopDescrambling(mCasSession);
    ret += Aml_MP_CAS_CloseSession(mCasSession);

    return 0;
}
////////////////////////////////////////////////////////////////////////////////
TsDemuxer::TsDemuxer(Aml_MP_DemuxType demuxType, Aml_MP_DemuxId demuxId)
{
    mDemux = AmlDemuxBase::create(demuxType);
    mDemux->open(false, demuxId, false);
    mDemux->start();
}

TsDemuxer::~TsDemuxer()
{
    MLOG();

    for (int i = AML_MP_STREAM_TYPE_UNKNOWN + 1; i < AML_MP_STREAM_TYPE_NB; ++i) {
        stopStream((Aml_MP_StreamType)i);
        closeStream((Aml_MP_StreamType)i);
    }

    mPids.clear();

    if (mDemux) {
        mDemux->stop();
        mDemux->close();
        mDemux.clear();
    }

    MLOGI("TsDemuxer destructor end.");
}

int TsDemuxer::feedTs(const uint8_t* data, size_t size)
{
    int ret = -1;

    if (mDemux == nullptr) {
        return -1;
    }

    ret = mDemux->feedTs(data, size);

    return ret;
}

void TsDemuxer::getFlowStatus(bool* underflow, bool* overflow)
{
    bool streamUnderflow = false;
    bool streamOverflow = false;

    for (auto& stream: mStreams) {
        stream.second.bufferQueue->getFlowStatus(&streamUnderflow, &streamOverflow);

        if (underflow) {
            *underflow |= streamUnderflow;
        }

        if (overflow) {
            *overflow |= streamOverflow;
        }
    }
}

int TsDemuxer::openStream(Aml_MP_StreamType streamType, size_t bufferCount, size_t bufferSize, bool isSVP, const std::function<StreamConsumer>& consumerCb)
{
    Stream& stream = mStreams[streamType];
    stream.consumerCb = consumerCb;

    if (stream.bufferQueue == nullptr) {
        stream.bufferQueue = new BufferQueue(streamType, bufferCount, bufferSize, isSVP);
    }

    if (stream.bufferQueueConsumer == nullptr) {
        const char* streamName = mpStreamType2Str(streamType);
        stream.bufferQueueConsumer = new BufferQueue::Consumer(streamType, streamName);
        stream.bufferQueueConsumer->connectQueue(stream.bufferQueue.get());
        stream.bufferQueueConsumer->connectSink([this, streamType](const uint8_t* data, size_t size, int64_t pts) {
            Stream& stream = mStreams[streamType];
            return stream.consumerCb(streamType, data, size, pts);
        });
    }

    stream.dumpFd = -1;

    stream.streamParser = new StreamParser(streamType, StreamParser::PES);
    stream.filterCbCount = 0;

    return 0;
}

int TsDemuxer::closeStream(Aml_MP_StreamType streamType)
{
    MLOG();
    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        return -1;
    }

    MLOG();
    if (stream->bufferQueueConsumer) {
        stream->bufferQueueConsumer->stop();
        stream->bufferQueueConsumer.clear();
    }
    MLOG();

    stream->bufferQueue.clear();

    if (stream->dumpFd >= 0) {
        ::close(stream->dumpFd);
        stream->dumpFd = -1;
    }

    stream->streamParser.clear();
    stream->filterCbCount = 0;

    MLOG();

    return 0;
}

int TsDemuxer::startStream(Aml_MP_StreamType streamType, int pid, Aml_MP_CodecID codec)
{
    int ret = 0;

    if (mPids.find(pid) != mPids.end()) {
        MLOGW("stream(%d) started already!", pid);
        return -1;
    }

    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        MLOGW("stream hasn't been open! type:%s", mpStreamType2Str(streamType));
        return -1;
    }

    Aml_MP_DemuxFilterParams params;
    memset(&params, 0, sizeof(params));
    if (streamType == AML_MP_STREAM_TYPE_AUDIO) {
        params.type =  AML_MP_DEMUX_FILTER_AUDIO;
    } else if (streamType == AML_MP_STREAM_TYPE_VIDEO) {
        params.type = AML_MP_DEMUX_FILTER_VIDEO;
    }
    params.codecType = codec;
    stream->channel = mDemux->createChannel(pid, &params);
    ret = mDemux->openChannel(stream->channel);
    if (ret < 0) {
        MLOGE("openChannel failed!");
        return ret;
    }

    stream->filter = mDemux->createFilter([](int pid, size_t size, const uint8_t* data, void* userData) -> int {
        int ret = 0;
        TsDemuxer* demuxer = (TsDemuxer*)userData;
        Aml_MP_StreamType streamType = demuxer->mPids[pid];
        Stream* stream = demuxer->getStream(streamType);
        if (stream == nullptr) {
            MLOGI("[%s] filter get stream failed", mpStreamType2Str(streamType));
            return -1;
        }

        if (stream->bufferQueue) {
            MLOGV("filter(%d) size:%zu(%zu), cbCount:%zu", pid, size, size-14, ++stream->filterCbCount);
            stream->streamParser->append(data, size);

            int ret = 0;
            StreamParser::Frame frame;
            while (true) {
                BufferQueue::Buffer* buffer;
                ret = stream->bufferQueue->dequeueBuffer(&buffer);
                if (ret < 0) {
                    MLOGI("[%s] filter dequeueBuffer failed, streamParser current BufferSize:%zu", mpStreamType2Str(streamType), stream->streamParser->totalBufferSize());
                    return -1;
                }

                ret = stream->streamParser->lockFrame(&frame);
                if (ret < 0) {
                    stream->bufferQueue->cancelBuffer(buffer);
                    break;
                }

                if (stream->dumpFd >= 0) {
                    int wlen = ::write(stream->dumpFd, frame.data, frame.size);
                    if (wlen != (int)frame.size) {
                        MLOGE("write dump file failed!");
                    }
                }

                buffer->append(frame.data, frame.size);
                buffer->setPts(frame.pts);
                stream->bufferQueue->queueBuffer(buffer);

                stream->streamParser->unlockFrame(&frame);
            }
        }

        return 0;
    }, this);

    ret = mDemux->attachFilter(stream->filter, stream->channel);
    if (ret < 0) {
        MLOGE("attachFilter failed, pid:%#x, stream:%s", pid, mpStreamType2Str(streamType));
    }

    stream->pid = pid;
    mPids.emplace(pid, streamType);

#if 0
    std::string filename = "/data/dump_stream_";
    filename.append(std::to_string(pid));
    filename.append(".es");
    stream->dumpFd = ::open(filename.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (stream->dumpFd < 0) {
        MLOGE("open dumpFd %s failed!", filename.c_str());
    }
#endif

    stream->bufferQueueConsumer->start();

    return 0;
}

int TsDemuxer::stopStream(Aml_MP_StreamType streamType)
{
    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        return -1;
    }

    stream->bufferQueueConsumer->pause();
    stream->bufferQueueConsumer->flush();

    mPids.erase(stream->pid);
    stream->pid = AML_MP_INVALID_PID;

    if (stream->filter != AML_MP_INVALID_HANDLE) {
        mDemux->detachFilter(stream->filter, stream->channel);
        mDemux->destroyFilter(stream->filter);
        stream->filter = AML_MP_INVALID_HANDLE;
    }

    if (stream->channel != AML_MP_INVALID_HANDLE) {
        mDemux->closeChannel(stream->channel);
        mDemux->destroyChannel(stream->channel);
        stream->channel = AML_MP_INVALID_HANDLE;
    }

    MLOG();

    return 0;
}

int TsDemuxer::pauseStream(Aml_MP_StreamType streamType)
{
    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        return -1;
    }

    stream->bufferQueueConsumer->pause();

    return 0;
}

int TsDemuxer::resumeStream(Aml_MP_StreamType streamType)
{
    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        return -1;
    }

    stream->bufferQueueConsumer->start();

    return 0;
}

int TsDemuxer::notifyStreamInputBufferDone(Aml_MP_StreamType streamType, int64_t handle)
{
    Stream* stream = getStream(streamType);
    if (stream == nullptr) {
        return -1;
    }

    stream->bufferQueueConsumer->notifyBufferReleased(handle);

    return 0;
}

TsDemuxer::Stream* TsDemuxer::getStream(Aml_MP_StreamType streamType)
{
    auto it = mStreams.find(streamType);
    if (it != mStreams.end()) {
        return &it->second;
    }

    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
Playback::Playback(Aml_MP_DemuxId demuxId, Aml_MP_InputSourceType sourceType, Aml_MP_InputStreamType inputStreamType, uint64_t options)
: mDemuxId(demuxId)
, mDrmMode(inputStreamType)
, mOptions(options)
{
    MLOGI("Playback structure,sourceType:%d\n", sourceType);
    Aml_MP_PlayerCreateParams createParams;
    memset(&createParams, 0, sizeof(createParams));
    createParams.channelId = AML_MP_CHANNEL_ID_AUTO;
    createParams.demuxId = demuxId;
    createParams.sourceType = sourceType;
    createParams.drmMode = inputStreamType;
    createParams.options = options;
    int ret = Aml_MP_Player_Create(&createParams, &mPlayer);
    if (ret < 0) {
        MLOGE("create player failed!");
        return;
    }

    if (sourceType == AML_MP_INPUT_SOURCE_ES_MEMORY) {
        mDemuxer = new TsDemuxer(AML_MP_DEMUX_TYPE_SOFTWARE, demuxId);
    }
}

Playback::~Playback()
{
    MLOGI("~Playback\n");
    stop();

    if (mDemuxer) {
        mDemuxer.clear();
    }

    if (mPlayer != AML_MP_INVALID_HANDLE) {
        Aml_MP_Player_Destroy(mPlayer);
        mPlayer = AML_MP_INVALID_HANDLE;
    }

    MLOGI("dtor playback end!");
}

int Playback::setAVSyncSource(Aml_MP_AVSyncSource syncSource)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);

    mSyncSource = syncSource;
    MLOGI("mSyncSource is %d", mSyncSource);

    return 0;
}

int Playback::setPcrPid(int pid)
{
    AML_MP_TRACE(10);
    std::unique_lock<std::mutex> _l(mLock);

    mPcrPid = pid;

    return 0;
}

int Playback::setSubtitleDisplayWindow(int x, int y, int width, int height) {
    MLOGI("setSubtitleDisplayWindow, x:%d, y: %d, width: %d, height: %d", x, y, width, height);
    int ret = Aml_MP_Player_SetSubtitleWindow(mPlayer, x, y,width, height);
    return ret;
}

int Playback::setVideoWindow(int x, int y, int width, int height)
{
    MLOGI("setVideoWindow, x:%d, y: %d, width: %d, height: %d", x, y, width, height);
    int ret = Aml_MP_Player_SetVideoWindow(mPlayer, x, y,width, height);
    return ret;
}

int Playback::setParameter(Aml_MP_PlayerParameterKey key, void* parameter)
{
    int ret = Aml_MP_Player_SetParameter(mPlayer, key, parameter);
    return ret;
}

#ifdef ANDROID
void Playback::setANativeWindow(const android::sp<ANativeWindow>& window)
{
    MLOGI("setANativeWindow");
    Aml_MP_Player_SetANativeWindow(mPlayer, window.get());
}
#endif

void Playback::playerRegisterEventCallback(Aml_MP_PlayerEventCallback cb, void* userData)
{
    mEventCallback = cb;
    mUserData = userData;
}

void Playback::eventCallback(Aml_MP_PlayerEventType eventType, int64_t param)
{
    MLOGD("Playback eventCallback event: %d, %s, param %" PRIx64 "\n", eventType, mpPlayerEventType2Str(eventType), param);
    switch (eventType) {
        case AML_MP_PLAYER_EVENT_VIDEO_OVERFLOW:
        {
            uint32_t video_overflow_num;
            video_overflow_num = *(uint32_t*)param;
            MLOGI("%s %d\n", mpPlayerEventType2Str(eventType), video_overflow_num);
        }
        break;

        case AML_MP_PLAYER_EVENT_VIDEO_UNDERFLOW:
        {
            uint32_t video_underflow_num;
            video_underflow_num = *(uint32_t*)param;
            MLOGI("%s %d\n", mpPlayerEventType2Str(eventType), video_underflow_num);
        }
        break;

        case AML_MP_PLAYER_EVENT_AUDIO_OVERFLOW:
        {
            uint32_t audio_overflow_num;
            audio_overflow_num = *(uint32_t*)param;
            MLOGI("%s %d\n", mpPlayerEventType2Str(eventType), audio_overflow_num);
        }
        break;

        case AML_MP_PLAYER_EVENT_AUDIO_UNDERFLOW:
        {
            uint32_t audio_underflow_num;
            audio_underflow_num = *(uint32_t*)param;
            MLOGI("%s %d\n", mpPlayerEventType2Str(eventType), audio_underflow_num);
        }
        break;

        case AML_MP_PLAYER_EVENT_VIDEO_INPUT_BUFFER_DONE:
        {
            if (mDemuxer) {
                mDemuxer->notifyStreamInputBufferDone(AML_MP_STREAM_TYPE_VIDEO, param);
            }
            break;
        }

        case AML_MP_PLAYER_EVENT_AUDIO_INPUT_BUFFER_DONE:
        {
            if (mDemuxer) {
                mDemuxer->notifyStreamInputBufferDone(AML_MP_STREAM_TYPE_AUDIO, param);
            }
            break;
        }

        default:
            break;
    }

    if (mEventCallback) {
        mEventCallback(mUserData, eventType, param);
    }
}

int Playback::start(const sptr<ProgramInfo>& programInfo, AML_MP_CASSESSION casSession, PlayMode playMode)
{
    mProgramInfo = programInfo;

    //printStreamsInfo();
    MLOGI(">>>> Playback start\n");
    mPlayMode = playMode;
    MLOGI("Playback start mPlayMode:%d\n", mPlayMode);
    int ret = 0;

    if (mPlayer == AML_MP_INVALID_HANDLE) {
        return -1;
    }

    bool useTif = false;
    Aml_MP_Player_SetParameter(mPlayer, AML_MP_PLAYER_PARAMETER_USE_TIF, &useTif);

    Aml_MP_Player_RegisterEventCallBack(mPlayer, [](void* userData, Aml_MP_PlayerEventType eventType, int64_t param) {
        static_cast<Playback*>(userData)->eventCallback(eventType, param);
    }, this);

    if (mProgramInfo->scrambled) {
        Aml_MP_Player_SetCasSession(mPlayer, casSession);
    }
    Aml_MP_Player_SetAVSyncSource(mPlayer, mSyncSource);
    if (mSyncSource == AML_MP_AVSYNC_SOURCE_PCR && mPcrPid != AML_MP_INVALID_PID) {
        Aml_MP_Player_SetPcrPid(mPlayer, mPcrPid);
    }

    if (mPlayMode == PlayMode::START_AUDIO_START_VIDEO) {
        if (setAudioParams()) {
            ret |= Aml_MP_Player_StartAudioDecoding(mPlayer);
        }

        if (setADParams()) {
            ret |= Aml_MP_Player_StartADDecoding(mPlayer);
        }

        if (setVideoParams()) {
            ret |= Aml_MP_Player_StartVideoDecoding(mPlayer);
        }

        if (setSubtitleParams()) {
            ret |= Aml_MP_Player_StartSubtitleDecoding(mPlayer);
        }
    } else if (mPlayMode == PlayMode::START_VIDEO_START_AUDIO) {
        if (setVideoParams()) {
            ret |= Aml_MP_Player_StartVideoDecoding(mPlayer);
        }

        if (setAudioParams()) {
            ret |= Aml_MP_Player_StartAudioDecoding(mPlayer);
        }

        if (setADParams()) {
            ret |= Aml_MP_Player_StartADDecoding(mPlayer);
        }

        if (setSubtitleParams()) {
            ret |= Aml_MP_Player_StartSubtitleDecoding(mPlayer);
        }
    } else if (mPlayMode == PlayMode::START_ALL_STOP_ALL || mPlayMode == PlayMode::START_ALL_STOP_SEPARATELY) {
        setAudioParams();
        setADParams();
        setVideoParams();
        setSubtitleParams();
        MLOGI(">>> Aml_MP_Player_Start\n");
        ret = Aml_MP_Player_Start(mPlayer);
    } else if (mPlayMode == PlayMode::START_SEPARATELY_STOP_ALL ||
            mPlayMode == PlayMode::START_SEPARATELY_STOP_SEPARATELY ||
            playMode == PlayMode::START_SEPARATELY_STOP_SEPARATELY_V2) {
        setAudioParams();
        setADParams();
        setVideoParams();
        setSubtitleParams();
        if (playMode == PlayMode::START_SEPARATELY_STOP_SEPARATELY_V2) {
            ret |= Aml_MP_Player_StartVideoDecoding(mPlayer);
            ret |= Aml_MP_Player_StartAudioDecoding(mPlayer);
            ret |= Aml_MP_Player_StartADDecoding(mPlayer);
        } else {
            ret |= Aml_MP_Player_StartAudioDecoding(mPlayer);
            ret |= Aml_MP_Player_StartADDecoding(mPlayer);
            ret |= Aml_MP_Player_StartVideoDecoding(mPlayer);
        }
        ret |= Aml_MP_Player_StartSubtitleDecoding(mPlayer);
    } else {
        MLOGE("unknown playmode:%d", mPlayMode);
    }

    if (mProgramInfo->subtitleCodec == AML_MP_SUBTITLE_CODEC_TELETEXT && mProgramInfo->magazine != -1 && mProgramInfo->page != -1) {
        // teletext need call ttControl after start
        AML_MP_TeletextCtrlParam teletextCtrlParam = {
            .magazine = mProgramInfo->magazine,
            .page = mProgramInfo->page,
            .event = AML_MP_TT_EVENT_GO_TO_SUBTITLE,
        };
        Aml_MP_Player_SetParameter(mPlayer, AML_MP_PLAYER_PARAMETER_TELETEXT_CONTROL, &teletextCtrlParam);
    }

    if (mDemuxer) {
        if (mProgramInfo->videoPid != AML_MP_INVALID_PID) {
            mDemuxer->openStream(AML_MP_STREAM_TYPE_VIDEO, 16, 3 * 1024 * 1024, mDrmMode == AML_MP_INPUT_STREAM_SECURE_MEMORY,
                [this](Aml_MP_StreamType streamType, const uint8_t* data, size_t size, int64_t pts) {
                    return Aml_MP_Player_WriteEsData(mPlayer, streamType, data, size, pts);
                });
            mDemuxer->startStream(AML_MP_STREAM_TYPE_VIDEO, mProgramInfo->videoPid, mProgramInfo->videoCodec);
        }

        if (mProgramInfo->audioPid != AML_MP_INVALID_PID) {
            mDemuxer->openStream(AML_MP_STREAM_TYPE_AUDIO, 128, 4 * 1024, false,
                [this](Aml_MP_StreamType streamType, const uint8_t* data, size_t size, int64_t pts) {
                    return Aml_MP_Player_WriteEsData(mPlayer, streamType, data, size, pts);
            });
            mDemuxer->startStream(AML_MP_STREAM_TYPE_AUDIO, mProgramInfo->audioPid, mProgramInfo->audioCodec);
        }
    }

    if (ret != 0) {
        MLOGE("player start failed!");
    }

    MLOGI("<<<< Playback start\n");
    usleep(350 * 1000);
    return ret;
}

bool Playback::setAudioParams()
{
    Aml_MP_AudioParams audioParams;
    memset(&audioParams, 0, sizeof(audioParams));
    audioParams.audioCodec = mProgramInfo->audioCodec;
    audioParams.pid = mProgramInfo->audioPid;
    MLOGI("setAudioParams apid:%d\n", audioParams.pid);
    Aml_MP_Player_SetAudioParams(mPlayer, &audioParams);

    return audioParams.pid != AML_MP_INVALID_PID;
}

bool Playback::setADParams()
{

    Aml_MP_AudioParams adParams;
    memset(&adParams, 0, sizeof(adParams));
    adParams.audioCodec = mProgramInfo->adCodec;
    adParams.pid = mProgramInfo->adPid;
    MLOGI("setADParams apid:%d\n", adParams.pid);
    Aml_MP_Player_SetADParams(mPlayer, &adParams);

    Aml_MP_Player_SetADVolume(mPlayer, 100);

    Aml_MP_ADVolume adVolume{50, 50};
    Aml_MP_Player_SetParameter(mPlayer, AML_MP_PLAYER_PARAMETER_AD_MIX_LEVEL, &adVolume);

    return adParams.pid != AML_MP_INVALID_PID;
}

bool Playback::setVideoParams()
{
    Aml_MP_VideoParams videoParams;
    memset(&videoParams, 0, sizeof(videoParams));
    videoParams.videoCodec = mProgramInfo->videoCodec;
    videoParams.pid = mProgramInfo->videoPid;
    MLOGI("setVideoParams vpid:%d\n", videoParams.pid);
    Aml_MP_Player_SetVideoParams(mPlayer, &videoParams);

    return videoParams.pid != AML_MP_INVALID_PID;
}

bool Playback::setSubtitleParams()
{
    Aml_MP_SubtitleParams subtitleParams{};
    subtitleParams.subtitleCodec = mProgramInfo->subtitleCodec;
    switch (subtitleParams.subtitleCodec) {
    case AML_MP_SUBTITLE_CODEC_DVB:
        subtitleParams.pid = mProgramInfo->subtitlePid;
        subtitleParams.compositionPageId = mProgramInfo->compositionPageId;
        subtitleParams.ancillaryPageId = mProgramInfo->ancillaryPageId;
        break;

    case AML_MP_SUBTITLE_CODEC_TELETEXT:
        subtitleParams.pid = mProgramInfo->subtitlePid;
        break;

    case AML_MP_SUBTITLE_CODEC_SCTE27:
        subtitleParams.pid = mProgramInfo->subtitlePid;
        break;

    case AML_MP_SUBTITLE_CODEC_TTML://TTML Subtitle Support
        subtitleParams.pid = mProgramInfo->subtitlePid;
        break;

    default:
        break;
    }

    if (subtitleParams.subtitleCodec != AML_MP_CODEC_UNKNOWN) {
        Aml_MP_Player_SetSubtitleParams(mPlayer, &subtitleParams);
        return true;
    }

    return false;
}

int Playback::stop()
{
    int ret = 0;

    if (mDemuxer) {
        mDemuxer->stopStream(AML_MP_STREAM_TYPE_VIDEO);
        mDemuxer->stopStream(AML_MP_STREAM_TYPE_AUDIO);
    }

    if (mPlayMode == PlayMode::START_ALL_STOP_ALL || mPlayMode == PlayMode::START_SEPARATELY_STOP_ALL) {
        ret = Aml_MP_Player_Stop(mPlayer);
        if (ret < 0) {
            MLOGE("player stop failed!");
        }
    } else {
        if (mPlayMode == PlayMode::START_VIDEO_START_AUDIO) {
            ret |= Aml_MP_Player_StopVideoDecoding(mPlayer);
            ret |= Aml_MP_Player_StopAudioDecoding(mPlayer);
            ret |= Aml_MP_Player_StopADDecoding(mPlayer);
        } else {
            ret |= Aml_MP_Player_StopAudioDecoding(mPlayer);
            ret |= Aml_MP_Player_StopADDecoding(mPlayer);
            ret |= Aml_MP_Player_StopVideoDecoding(mPlayer);
        }
        ret |= Aml_MP_Player_StopSubtitleDecoding(mPlayer);

        if (ret != 0) {
            MLOGE("player stop separately failed!");
        }
    }

    return ret;
}

void Playback::signalQuit()
{
    MLOGI("signalQuit!");
}

int Playback::writeData(const uint8_t* buffer, size_t size)
{
    int wlen = -1;

    if (mDemuxer) {
        bool overflow = false;
        mDemuxer->getFlowStatus(nullptr, &overflow);
        if (overflow) {
            //MLOGI("overflow!");
            return -1;
        }
        wlen = mDemuxer->feedTs(buffer, size);
        if (wlen != (int)size) {
            MLOGW("feedTs return %d, expected:%zu", wlen, size);
        }
    } else {
        wlen = Aml_MP_Player_WriteData(mPlayer, buffer, size);
    }

    return wlen;
}

void Playback::printStreamsInfo()
{
    if (mProgramInfo == nullptr) {
        return;
    }

    for (size_t i = 0; i < mProgramInfo->videoStreams.size(); i++) {
        if (i == 0) {
            printf("Video Streams:\n");
        }

        printf("\tVideo[%zu] pid: %d, codec: %d\n", i, mProgramInfo->videoStreams.at(i).pid, mProgramInfo->videoStreams.at(i).codecId);
    }

    for (size_t i = 0; i < mProgramInfo->audioStreams.size(); i++) {
        if (i == 0) {
            printf("Audio streams:\n");
        }
        printf("\tAudio[%zu] pid: %d, codec: %d\n", i, mProgramInfo->audioStreams.at(i).pid, mProgramInfo->audioStreams.at(i).codecId);
    }

    for (size_t i = 0; i < mProgramInfo->subtitleStreams.size(); i++) {
        if (i == 0) {
            printf("Subtitle streams:\n");
        }
        printf("\tSubtitle[%zu] pid: %d, codec: %d", i, mProgramInfo->subtitleStreams.at(i).pid, mProgramInfo->subtitleStreams.at(i).codecId);
        if (mProgramInfo->subtitleStreams.at(i).codecId == AML_MP_SUBTITLE_CODEC_DVB) {
            printf("[%d, %d]\n", mProgramInfo->subtitleStreams.at(i).compositionPageId, mProgramInfo->subtitleStreams.at(i).ancillaryPageId);
        } else {
            printf("\n");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
static struct TestModule::Command g_commandTable[] = {
    {
        "help", 0, "help",
        [](AML_MP_PLAYER player __unused, const std::vector<std::string>& args __unused) -> int {
            TestModule::printCommands(g_commandTable, true);
            return 0;
        }
    },

    {
        "pause", 0, "pause player",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_Player_Pause(player);
        }
    },

    {
        "resume", 0, "resume player",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_Player_Resume(player);
        }
    },

    {
        "hide", 0, "hide video",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_Player_HideVideo(player);
        }
    },

    {
        "show", 0, "show video",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            return Aml_MP_Player_ShowVideo(player);
        }
    },

    {
        "gShowState", 0, "get show state",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            bool state;
            int ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_SHOW_STATE, &state);
            printf("now video show state: %s(%d)\n", state ? "show" : "hide", ret);
            return ret;
        }
    },

    {
        "flush", 0, "call flush",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_Player_Flush(player);
            printf("call flush ret: %d\n", ret);
            return ret;
        }
    },

    {
        "switchAudio", 0, "switch audio",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_AudioParams audioParams;
            int ret;
            int audioCodec, audioPid;
            if (args.size() != 3) {
                printf("Input example: switchAudio Pid codec_ID\n");
                return -1;
            }
            printf("String input: %s %s\n", args[1].data(), args[2].data());
            audioPid = stof(args[1]);
            audioCodec = stof(args[2]);
            memset(&audioParams, 0, sizeof(audioParams));
            audioParams.audioCodec = (Aml_MP_CodecID)audioCodec;
            audioParams.pid = audioPid;
            ret = Aml_MP_Player_SwitchAudioTrack(player, &audioParams);
            printf("Switch audio param[%d, %d], ret: %d\n", audioParams.pid, audioParams.audioCodec, ret);
            return ret;
        }
    },

    {
        "switchSubtitle", 0, "switch subtitle",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_SubtitleParams subtitleParams;
            int ret;
            int subtitleCodec, subtitlePid, compositionPageId, ancillaryPageId;
            if (args.size() != 5) {
                printf("Input example: switchSubtitle Pid codec_ID compositionPageId ancillaryPageId\n");
                return -1;
            }
            printf("String input: %s %s\n", args[1].data(), args[2].data());
            subtitlePid = stof(args[1]);
            subtitleCodec = stof(args[2]);
            compositionPageId = stof(args[3]);
            ancillaryPageId = stof(args[4]);
            memset(&subtitleParams, 0, sizeof(subtitleParams));
            subtitleParams.subtitleCodec = (Aml_MP_CodecID)subtitleCodec;
            subtitleParams.pid = subtitlePid;
            if (subtitleParams.subtitleCodec == AML_MP_SUBTITLE_CODEC_DVB) {
                subtitleParams.compositionPageId = compositionPageId;
                subtitleParams.ancillaryPageId = ancillaryPageId;
            }
            ret = Aml_MP_Player_SwitchSubtitleTrack(player, &subtitleParams);
            printf("Switch audio param[%d, %d], ret: %d\n", subtitleParams.pid, subtitleParams.subtitleCodec, ret);
            return ret;
        }
    },

    {
        "gPts", 0, "get Pts",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int64_t pts;
            int ret = Aml_MP_Player_GetCurrentPts(player, AML_MP_STREAM_TYPE_VIDEO, &pts);
            printf("Current video pts: 0x%" PRIx64 ", ret: %d\n", pts, ret);
            ret = Aml_MP_Player_GetCurrentPts(player, AML_MP_STREAM_TYPE_AUDIO, &pts);
            printf("Current audio pts: 0x%" PRIx64 ", ret: %d\n", pts, ret);
            return ret;
        }
    },

    {
        "gFPts", 0, "get FPts",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int64_t pts;
            int ret = Aml_MP_Player_GetFirstPts(player, AML_MP_STREAM_TYPE_VIDEO, &pts);
            printf("first video pts: 0x%" PRIx64 ", ret: %d\n", pts, ret);
            ret = Aml_MP_Player_GetFirstPts(player, AML_MP_STREAM_TYPE_AUDIO, &pts);
            printf("first audio pts: 0x%" PRIx64 ", ret: %d\n", pts, ret);
            return ret;
        }
    },

    {
        "gVolume", 0, "get volume",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            float volume;
            int ret = Aml_MP_Player_GetVolume(player, &volume);
            printf("Current volume: %f, ret: %d\n", volume, ret);
            return ret;
        }
    },

    {
        "sVolume", 0, "set volume",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            float volume, volume2;
            if (args.size() != 2) {
                printf("Input example: sVolume volume\n");
                return -1;
            }
            printf("String input: %s\n", args[1].data());
            volume = stof(args[1]);
            int ret = Aml_MP_Player_SetVolume(player,volume);
            ret = Aml_MP_Player_GetVolume(player, &volume2);
            printf("Get volume: %f, set volume: %f, ret: %d\n", volume2, volume, ret);
            return ret;
        }
    },

    {
        "gMute", 0, "get audio mute/unmute state",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int mute;
            int ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_MUTE, &mute);
            printf("Current mute state: %d, ret: %d\n", mute, ret);
            return ret;
        }
    },

    {
        "sMute", 0, "set audio mute/unmute state",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int mute, mute2;
            if (args.size() != 2) {
                printf("Input example: sMute mute\n");
                return -1;
            }
            printf("String input: %s\n", args[1].data());
            mute = stod(args[1]);
            int ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_MUTE, &mute);
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_MUTE, &mute2);
            printf("Get mute state: %d, set mute: %d, ret: %d\n", mute2, mute, ret);
            return ret;
        }
    },

    {
        "sFast", 0, "set Fast rate",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            float fastRate, getRate;
            int ret;
            if (args.size() != 2) {
                printf("Input example: sFast rate\n");
                return -1;
            }
            printf("String input: %s\n", args[1].data());
            fastRate = stof(args[1]);
            ret = Aml_MP_Player_SetPlaybackRate(player, fastRate);
            Aml_MP_Player_GetPlaybackRate(player, &getRate);
            printf("set rate: %f(%f), ret: %d\n", fastRate, getRate, ret);
            return ret;
        }
    },

    {
        "gFast", 0, "get Fast rate",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            float fastRate;
            int ret;
            ret = Aml_MP_Player_GetPlaybackRate(player, &fastRate);
            printf("get rate: %f, ret: %d\n", fastRate, ret);
            return ret;
        }
    },

    {
        "gDecodingState", 0, "get a/v decoding state",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret;
            AML_MP_DecodingState decodingState;
            ret = Aml_MP_Player_GetDecodingState(player, AML_MP_STREAM_TYPE_VIDEO, &decodingState);
            printf("get video decoding state: %d, ret: %d\n", decodingState, ret);
            ret = Aml_MP_Player_GetDecodingState(player, AML_MP_STREAM_TYPE_AUDIO, &decodingState);
            printf("get audio decoding state: %d, ret: %d\n", decodingState, ret);
            return ret;
        }
    },

    {
        "gBuffer", 0, "get Buffer state",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_BufferStat bufferStat;
            int ret = Aml_MP_Player_GetBufferStat(player, &bufferStat);
            printf("Audio buffer stat: size: %d, dataLen: %d, bufferedMs: %d\n", bufferStat.audioBuffer.size, bufferStat.audioBuffer.dataLen, bufferStat.audioBuffer.bufferedMs);
            printf("Video buffer stat: size: %d, dataLen: %d, bufferedMs: %d\n", bufferStat.videoBuffer.size, bufferStat.videoBuffer.dataLen, bufferStat.videoBuffer.bufferedMs);
            return ret;
        }
    },

    {
        "sWindow", 0, "set video window",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int32_t x, y, width, height;
            if (args.size() != 5) {
                printf("Input example: sWindow x y width height\n");
                return -1;
            }
            printf("String input x: %s, y: %s, width: %s, height: %s\n", args[1].data(), args[2].data(), args[3].data(), args[4].data());
            x = stoi(args[1]);
            y = stoi(args[2]);
            width = stoi(args[3]);
            height = stoi(args[4]);
            int ret = Aml_MP_Player_SetVideoWindow(player, x, y,width, height);
            printf("set x: %d, y: %d, width: %d, height: %d, ret: %d\n", x, y, width, height, ret);
            return ret;
        }
    },

    {
        "sZorder", 0, "set zorder",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int32_t zorder;
            if (args.size() != 2) {
                printf("Input example: sZorder zorder\n");
                return -1;
            }
            printf("String input zorder: %s\n", args[1].data());
            zorder = stoi(args[1]);
            int ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_WINDOW_ZORDER, &zorder);
            printf("set zorder: %d, ret: %d\n", zorder, ret);
            return ret;
        }
    },

    {
        "sDspMode", 0, "set display mode",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_VideoDisplayMode videoMode = AML_MP_VIDEO_DISPLAY_MODE_NORMAL;
            if (args.size() != 2) {
                printf("Input example: sDspMode display_mode\n");
                return -1;
            }
            videoMode = (Aml_MP_VideoDisplayMode)stoi(args[1]);
            int ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE, &videoMode);
            printf("AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE set mode: %d, ret: %d\n", videoMode, ret);
            return ret;
        }
    },

    {
        "sAfdMode", 0, "set afd aspect mode",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_VideoAFDAspectMode aspectMode = AML_MP_VIDEO_AFD_ASPECT_MODE_AUTO;
            if (args.size() != 2) {
                printf("Input example: sAfdMode aspect_mode\n");
                return -1;
            }
            aspectMode = (Aml_MP_VideoAFDAspectMode)stoi(args[1]);
            int ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_AFD_ASPECT_MODE, &aspectMode);
            printf("AML_MP_PLAYER_PARAMETER_VIDEO_AFD_ASPECT_MODE set mode: %d, ret: %d\n", aspectMode, ret);
            return ret;
        }
    },

    {
        "sParam", 0, "set param",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_VideoDisplayMode videoMode = AML_MP_VIDEO_DISPLAY_MODE_NORMAL;
            int ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE, &videoMode);
            printf("AML_MP_PLAYER_PARAMETER_VIDEO_DISPLAY_MODE set mode: %d, ret: %d\n", videoMode, ret);
            bool blackOut = true;
            ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_BLACK_OUT, &blackOut);
            printf("AML_MP_PLAYER_PARAMETER_BLACK_OUT set mode: %d, ret: %d\n", blackOut, ret);
            Aml_MP_AudioOutputMode audioMode = AML_MP_AUDIO_OUTPUT_PCM;
            ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_MODE, &audioMode);
            printf("AML_MP_PLAYER_PARAMETER_AUDIO_OUTPUT_MODE set mode: %d, ret: %d\n", audioMode, ret);
            Aml_MP_ADVolume adVolume = {50, 50};
            ret = Aml_MP_Player_SetParameter(player, AML_MP_PLAYER_PARAMETER_AD_MIX_LEVEL, &adVolume);
            printf("AML_MP_PLAYER_PARAMETER_AD_MIX_LEVEL set mode: %d, %d, ret: %d\n", adVolume.masterVolume, adVolume.slaveVolume, ret);
            return ret;
        }
    },

    {
        "gParam", 0, "get param",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            Aml_MP_VideoInfo videoInfo;
            int ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_INFO, &videoInfo);
            printf("AML_MP_PLAYER_PARAMETER_VIDEO_INFO, width: %d, height: %d, framerate: %d, bitrate: %d, ratio64: %d, ret: %d\n", videoInfo.width, videoInfo.height, videoInfo.frameRate, videoInfo.bitrate, videoInfo.ratio64, ret);
            Aml_MP_VdecStat vdecStat;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_VIDEO_DECODE_STAT, &vdecStat);
            printf("AML_MP_PLAYER_PARAMETER_VIDEO_DECODE_STAT, frame_width: %d, frame_height: %d, frame_rate: %d, ret: %d\n", vdecStat.frame_width, vdecStat.frame_height, vdecStat.frame_rate, ret);
            Aml_MP_AudioInfo audioInfo;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_INFO, &audioInfo);
            printf("AML_MP_PLAYER_PARAMETER_AUDIO_INFO, sample_rate: %d, channels: %d, channel_mask: %d, bitrate: %d, ret: %d\n", audioInfo.sample_rate, audioInfo.channels, audioInfo.channel_mask, audioInfo.bitrate, ret);
            Aml_MP_AdecStat adecStat;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AUDIO_DECODE_STAT, &adecStat);
            printf("AML_MP_PLAYER_PARAMETER_AUDIO_DECODE_STAT get audio decode stat frame_count: %d, error_frame_count: %d, drop_frame_count: %d, ret: %d\n", adecStat.frame_count, adecStat.error_frame_count, adecStat.drop_frame_count, ret);
            Aml_MP_AudioInfo adInfo;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AD_INFO, &adInfo);
            printf("AML_MP_PLAYER_PARAMETER_AD_INFO, sample_rate: %d, channels: %d, channel_mask: %d, bitrate: %d, ret: %d\n", adInfo.sample_rate, adInfo.channels, adInfo.channel_mask, adInfo.bitrate, ret);
            Aml_MP_AdecStat adStat;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AD_DECODE_STAT, &adStat);
            printf("AML_MP_PLAYER_PARAMETER_AD_DECODE_STAT get audio decode stat frame_count: %d, error_frame_count: %d, drop_frame_count: %d, ret: %d\n", adStat.frame_count, adStat.error_frame_count, adStat.drop_frame_count, ret);
            uint32_t instanceId;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_INSTANCE_ID, &instanceId);
            printf("AML_MP_PLAYER_PARAMETER_INSTANCE_ID get instance id: %d\n", instanceId);
            int32_t syncId = -1;
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_SYNC_ID, &syncId);
            printf("AML_MP_PLAYER_PARAMETER_SYNC_ID get sync id: %d\n", syncId);

            Aml_MP_AvInfo avState;
            avState.dataLength = 1024;
            avState.streamTypeMask = AML_MP_STREAM_TYPE_MASK_VIDEO | AML_MP_STREAM_TYPE_MASK_AUDIO;
            avState.data = (uint8_t *)malloc(avState.dataLength);
            ret = Aml_MP_Player_GetParameter(player, AML_MP_PLAYER_PARAMETER_AV_INFO_JSON, &avState);
            printf("AML_MP_PLAYER_PARAMETER_AV_INFO_JSON, json[%zu]:\n%s\n", avState.actualLength, avState.data);
            free(avState.data);
            avState.data = NULL;
            return ret;
        }
    },

    {
        "sSync", 0, "set sync mode",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_Player_SetAVSyncSource(player, AML_MP_AVSYNC_SOURCE_PCR);
            printf("set sync mode: %d, ret: %d\n", AML_MP_AVSYNC_SOURCE_PCR, ret);
            return ret;
        }
    },

    {
        "start", 0, "call start",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_Player_Start(player);
            printf("call start ret: %d\n", ret);
            return ret;
        }
    },

    {
        "stop", 0, "call stop",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_Player_Stop(player);
            printf("call stop ret: %d\n", ret);
            return ret;
        }
    },

    {
        "gDecCap", 0, "get decoder capability",
        [](AML_MP_PLAYER player __unused, const std::vector<std::string>& args __unused) -> int {
            int32_t decType;
            char jsonInfo[1000]{0};
            int ret;
            if (args.size() != 2) {
                printf("Input example: gDecCap decoder_id\n");
                printf("Run getDecCapDemo\n");
                ret = Aml_MP_GetCodecCapability(AML_MP_VIDEO_CODEC_H264, jsonInfo, 1000);
                printf("Aml_MP_GetCodecCapability video AML_MP_VIDEO_CODEC_H264 json(%d): %s\n", ret, jsonInfo);
                ret = Aml_MP_GetCodecCapability(AML_MP_AUDIO_CODEC_MP3, jsonInfo, 1000);
                printf("Aml_MP_GetCodecCapability audio AML_MP_AUDIO_CODEC_MP3 json(%d): %s\n", ret, jsonInfo);
            } else {
                decType = stoi(args[1]);
                ret = Aml_MP_GetCodecCapability((Aml_MP_CodecID)decType, jsonInfo, 1000);
                printf("Aml_MP_GetCodecCapability json(%d): %s\n", ret, jsonInfo);
            }
            return ret;
        }
    },

    {
        "destroy", 0, "call destroy",
        [](AML_MP_PLAYER player, const std::vector<std::string>& args __unused) -> int {
            int ret = Aml_MP_Player_Destroy(player);
            printf("call destroy ret: %d\n", ret);
            return ret;
        }
    },

    {nullptr, 0, nullptr, nullptr}
};

const TestModule::Command* Playback::getCommandTable() const
{
    return g_commandTable;
}

void* Playback::getCommandHandle() const
{
    return mPlayer;
}

}
