/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpPlayerDemo_Parser"
#include <utils/AmlMpLog.h>
#include "AmlTsParser.h"
#include <vector>
#include <utils/AmlMpUtils.h>

static const char* mName = LOG_TAG;

namespace aml_mp {
///////////////////////////////////////////////////////////////////////////////
struct StreamType {
    int esStreamType;
    Aml_MP_StreamType mpStreamType;
    Aml_MP_CodecID codecId;
};

static const StreamType g_streamTypes[] = {
    {0x01, AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_MPEG12},
    {0x02, AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_MPEG12},
    {0x03, AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_MP3},
    {0x04, AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_MP3},
    {0x0f, AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_AAC},
    {0x10, AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_MPEG4},
    {0x11, AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_LATM},
    {0x1b, AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_H264},
    {0x24, AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_HEVC},
    {0x81, AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_AC3},
    {0x82, AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_SUBTITLE_CODEC_SCTE27},
    {0, AML_MP_STREAM_TYPE_UNKNOWN, AML_MP_CODEC_UNKNOWN},
};

static const StreamType g_descTypes[] = {
    { 0x6a, AML_MP_STREAM_TYPE_AUDIO,    AML_MP_AUDIO_CODEC_AC3          }, /* AC-3 descriptor */
    { 0x7a, AML_MP_STREAM_TYPE_AUDIO,    AML_MP_AUDIO_CODEC_EAC3         }, /* E-AC-3 descriptor */
    { 0x7b, AML_MP_STREAM_TYPE_AUDIO,    AML_MP_AUDIO_CODEC_DTS          },
    { 0x56, AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_SUBTITLE_CODEC_TELETEXT },
    { 0x59, AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_SUBTITLE_CODEC_DVB }, /* subtitling descriptor */
    { 0, AML_MP_STREAM_TYPE_UNKNOWN, AML_MP_CODEC_UNKNOWN},
};

#define MKTAG(a,b,c,d) ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
static const StreamType g_identifierTypes[] = {
    { MKTAG('A', 'C', '-', '3'), AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_AC3},
    { MKTAG('E', 'A', 'C', '3'), AML_MP_STREAM_TYPE_AUDIO, AML_MP_AUDIO_CODEC_EAC3},
    { MKTAG('H', 'E', 'V', 'C'), AML_MP_STREAM_TYPE_VIDEO, AML_MP_VIDEO_CODEC_HEVC},
    {0, AML_MP_STREAM_TYPE_UNKNOWN, AML_MP_CODEC_UNKNOWN},
};

static const StreamType g_extDescTypes[] = {
    { 0x15, AML_MP_STREAM_TYPE_AUDIO,    AML_MP_AUDIO_CODEC_AC4          }, /* AC-4 descriptor */
    { 0x20, AML_MP_STREAM_TYPE_SUBTITLE, AML_MP_SUBTITLE_CODEC_TTML },/* subtitling descriptor */
    {0, AML_MP_STREAM_TYPE_UNKNOWN, AML_MP_CODEC_UNKNOWN},
};

static const struct StreamType* getStreamTypeInfo(int esStreamType, const StreamType* table = g_streamTypes)
{
    const struct StreamType* result = nullptr;
    const StreamType *s = table;

    for (; s->esStreamType; s++) {
        if (esStreamType == s->esStreamType) {
            result = s;
            break;
        }
    }

    return result;
}

void ProgramInfo::debugLog() const
{
    MLOGI("ProgramInfo: programNumber=%d, pid=%d", programNumber, pmtPid);
    for (auto it : videoStreams) {
        MLOGI("ProgramInfo: videoStream: pid:%d, codecId:%d", it.pid, it.codecId);
    }
    for (auto it : audioStreams) {
        MLOGI("ProgramInfo: audioStream: pid:%d, codecId:%d", it.pid, it.codecId);
    }
    for (auto it : subtitleStreams) {
        MLOGI("ProgramInfo: subtitleStream: pid:%d, codecId:%d", it.pid, it.codecId);
    }
    if(scrambled) {
        MLOGI("ProgramInfo: is scrambled, caSystemId:0x%04X, ecmPid:0x%04X, privateDataLength:%d", caSystemId, ecmPid[0], privateDataLength);
        std::string privateDataHex;
        char hex[3];
        for(int i = 0; i < privateDataLength; i++){
            snprintf(hex, sizeof(hex), "%02X", privateData[i]);
            privateDataHex.append(hex);
            privateDataHex.append(" ");
        }
        MLOGI("ProgramInfo: privateData: %s", privateDataHex.c_str());
    }
}

///////////////////////////////////////////////////////////////////////////////
Parser::Parser(Aml_MP_DemuxId demuxId, bool isHardwareSource, Aml_MP_DemuxType demuxType, bool isSecureBuffer)
: mIsHardwareSource(isHardwareSource)
, mDemuxType(demuxType)
, mIsSecureBuffer(isSecureBuffer)
, mDemuxId(demuxId)
, mProgramInfo(new ProgramInfo)
{

}

Parser::~Parser()
{
    close();
}

int Parser::open()
{
    Aml_MP_DemuxType demuxType = mDemuxType;
    if (mIsHardwareSource) {
        demuxType = AML_MP_DEMUX_TYPE_HARDWARE;
    }

    mDemux = AmlDemuxBase::create(demuxType);
    if (mDemux == nullptr) {
        MLOGE("create demux failed!");
        return -1;
    }

    int ret = mDemux->open(mIsHardwareSource, mDemuxId, mIsSecureBuffer);
    if (ret < 0) {
        MLOGE("demux open failed!");
        return -1;
    }

    ret = mDemux->start();
    if (ret < 0) {
        MLOGE("demux start failed!");
        return -1;
    }

    return 0;
}

void Parser::selectProgram(int programNumber)
{
    std::lock_guard<std::mutex> _l(mLock);
    mProgramNumber = programNumber;
}

void Parser::selectProgram(int vPid, int aPid)
{
    std::lock_guard<std::mutex> _l(mLock);
    mVPid = vPid;
    mAPid = aPid;
}

void Parser::parseProgramInfoAsync()
{
    addSectionFilter(0, patCb, this);
    addSectionFilter(1, catCb, this);
}

int Parser::waitCATSectionDataParsed()
{
    std::unique_lock<std::mutex> l(mLock);
    bool ret = mCond.wait_for(l, std::chrono::seconds(60), [this] {
        return mCATParseDone || mRequestQuit;
    });

    return ret ? 0 : -1;
}

int Parser::waitProgramInfoParsed()
{
    std::unique_lock<std::mutex> l(mLock);
    bool ret = mCond.wait_for(l, std::chrono::seconds(60), [this] {
        return mParseDone || mRequestQuit;
    });

    return ret ? 0 : -1;
}

sptr<ProgramInfo> Parser::parseProgramInfo()
{
    parseProgramInfoAsync();
    waitProgramInfoParsed();
    return getProgramInfo();
}

void Parser::setEventCallback(const std::function<ProgramEventCallback>& cb)
{
    std::lock_guard<std::mutex> _l(mLock);
    mCb = cb;
}

int Parser::close()
{
    clearAllFilters();
    sptr<AmlDemuxBase> dmxTemp;
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mDemux != nullptr) {
            dmxTemp = mDemux;
            mDemux.clear();
        }
    }
    if (dmxTemp != nullptr) {
        dmxTemp->stop();
        dmxTemp->close();
    }

    MLOGI("%s:%d, end", __FUNCTION__, __LINE__);
    return 0;
}

void Parser::signalQuit()
{
    mRequestQuit = true;

    std::unique_lock<std::mutex> l(mLock);
    mCond.notify_all();
}

sptr<ProgramInfo> Parser::getProgramInfo() const
{
    std::lock_guard<std::mutex> _l(mLock);
    sptr<ProgramInfo> info = mProgramInfo;

    if (info && info->isComplete()) {
        return info;
    }

    return nullptr;
}

int Parser::writeData(const uint8_t* buffer, size_t size)
{
    int wlen = -1;
    //MLOGV("writeData:%p, size:%d", buffer, size);
    sptr<AmlDemuxBase> demux;
    {
        std::lock_guard<std::mutex> _l(mLock);
        demux = mDemux;
    }

    if (demux) {
        wlen = demux->feedTs(buffer, size);
    }

    return wlen;
}

int Parser::patCb(int pid, size_t size, const uint8_t* data, void* userData)
{
    AML_MP_UNUSED(pid);
    Parser* parser = (Parser*)userData;

    MLOGI("pat cb, size:%zu", size);
    Section section(data, size);
    const uint8_t* p = section.data();
    //int table_id = p[0];
    int section_syntax_indicator = (p[1]&0x80) >> 7;
    AML_MP_CHECK_EQ(section_syntax_indicator, 1);
    int section_length = (p[1] & 0x0F) << 4 | p[2];
    AML_MP_CHECK_LE(section_length, 4093);
    MLOGI("section_length = %d, size:%zu", section_length, size);

    PATSection result;
    p = section.advance(3);
    result.version_number = (p[2] & 0x3E) >> 1;
    //int transport_stream_id = p[0]<<8 | p[1];
    p = section.advance(5);
    int numPrograms = (section.dataSize() - 4)/4;

    for (int i = 0; i < numPrograms; ++i) {
        int program_number = p[0]<<8 | p[1];

        if (program_number != 0) {
            int program_map_PID = (p[2]&0x01F) << 8 | p[3];
            MLOGI("programNumber:%d, program_map_PID = %d\n", program_number, program_map_PID);
            result.pmtInfos.push_back({program_number, program_map_PID});
        } else {
            //int network_PID = (p[2]&0x1F) << 8 | p[3];
        }

        p = section.advance(4);
    }

    if (parser) {
        parser->onPatParsed(result);
    }

    return 0;
}

int Parser::pmtCb(int pid, size_t size, const uint8_t* data, void* userData)
{
    Parser* parser = (Parser*)userData;

    MLOGI("pmt cb, pid:%d, size:%zu", pid, size);
    Section section(data, size);
    const uint8_t* p = section.data();
    //int table_id = p[0];
    int section_syntax_indicator = (p[1]&0x80) >> 7;
    if (section_syntax_indicator != 1) {
        MLOGE("pmt section_syntax_indicator CHECK failed!");
        return -1;
    }
    int section_length = (p[1] & 0x0F) << 4 | p[2];
    AML_MP_CHECK_LE(section_length, 4093);
    MLOGI("section_length = %d, size:%zu", section_length, size);

    PMTSection results;
    results.pmtPid = pid;

    p = section.advance(3);
    int programNumber = p[0]<<8 | p[1];
    results.programNumber = programNumber;
    //check version_number and current_next_indicator to skip same pmt
    results.version_number = (p[2] & 0x3E) >> 1;
    results.current_next_indicator = p[2] & 0x01;
    //MLOGI("pmt cb, version_number:%d, current_next_indicator:%d", results.version_number, results.current_next_indicator);
    if (results.current_next_indicator == 0) {
        //MLOGI("just skip this pmt, because the current_next_indicator is zero");
        return 0;
    }
    if (parser) {
        // check version_number is same
        auto it = parser->mPidPmtMap.find(pid);
        if (it != parser->mPidPmtMap.end()) {
            if (results.version_number == it->second.version_number) {
                //MLOGI("just skip this pmt, because the version_number(%d) is same", results.version_number);
                return 0;
            }
        }
    }

    p = section.advance(5);
    int pcr_pid = (p[0]&0x1F)<<8 | p[1];
    results.pcrPid = pcr_pid;
    int program_info_length = (p[2]&0x0F) << 8 | p[3];
    AML_MP_CHECK_LT(program_info_length, 1024);
    p = section.advance(4);

    results.privateDataLength = 0;
    if (program_info_length > 0) {
        int descriptorsRemaining = program_info_length;
        const uint8_t* p2 = p;
        int count = 0;
        while (descriptorsRemaining >= 2) {
            int descriptor_tag = p2[0];
            int descriptor_length = p2[1];
            switch (descriptor_tag) {
            case 0x09:
            {
                int ca_system_id = p2[2]<<8 | p2[3];
                int ecm_pid = (p2[4]&0x1F)<<8 | p2[5];
                MLOGI("ca_system_id:%#x, ecm_pid:%#x, count:%d, descriptor_length:%d\n", ca_system_id, ecm_pid, ++count, descriptor_length);

                results.scrambled = true;
                results.caSystemId = ca_system_id;
                results.ecmPid = ecm_pid;
                results.privateDataLength = descriptor_length - 4;
                memcpy(results.privateData, &p2[6], results.privateDataLength);
            }
            break;

            case 0x65:
            {
                int scrambleAlgorithm = p[2];
                MLOGI("scrambleAlgorithm:%d", scrambleAlgorithm);

                results.scrambleAlgorithm = scrambleAlgorithm;
            }
            break;

            default:
                MLOGI("descriptor_tag:%#x", descriptor_tag);
                break;
            }

            p2 += 2 + descriptor_length;
            descriptorsRemaining -= 2 + descriptor_length;
        }
    }

    //skip program info
    p = section.advance(program_info_length);
    int infoBytesRemaining = section.dataSize() - 4;

    while (infoBytesRemaining >= 5) {
        int stream_type = p[0];
        int elementary_pid = (p[1]&0x1F) << 8 | p[2];
        int es_info_length = (p[3]&0x0F) << 8 | p[4];
        p = section.advance(5);
        infoBytesRemaining -= 5;

        PMTStream esStream;
        esStream.streamType = stream_type;
        esStream.streamPid = elementary_pid;

        if (es_info_length > 0) {
            int descriptorsRemaining = es_info_length;
            const uint8_t* p2 = p;
            int count = 0;
            while (descriptorsRemaining >= 2) {
                int descriptor_tag = p2[0];
                int descriptor_length = p2[1];

                esStream.descriptorTags[esStream.descriptorCount++] = descriptor_tag;

                switch (descriptor_tag) {
                case 0x09:
                {
                    int ca_system_id = p2[2]<<8 | p2[3];
                    int ecm_pid = (p2[4]&0x1F)<<8 | p2[5];
                    MLOGI("streamType:%#x, pid:%d, ca_system_id:%#x, ecm_pid:%#x, count:%d, descriptor_length:%d\n",
                            stream_type, elementary_pid, ca_system_id, ecm_pid, ++count, descriptor_length);
                    if (descriptor_length > 4) {
                        int has_iv = p2[6] & 0x1;
                        int aligned = ( p2[6] & 0x4 ) >> 2;
                        int scramble_mode = ( p2[6] & 0x8 ) >> 3;
                        int algorithm = ( p2[6] & 0xE0 ) >> 5;
                        MLOGI("%s, @@this is ca_private_data, data:%#x, iv:%d, aligned:%d, mode:%d, algo:%d",
                                  __FUNCTION__, p2[6], has_iv, aligned, scramble_mode, algorithm );
                        if ( algorithm == 1 ) {
                            results.scrambleInfo.algo = SCRAMBLE_ALGO_AES;
                            if ( scramble_mode == 0 ) {
                                results.scrambleInfo.mode = SCRAMBLE_MODE_ECB;
                            } else {
                                results.scrambleInfo.mode = SCRAMBLE_MODE_CBC;
                            }
                            results.scrambleInfo.alignment = (SCRAMBLE_ALIGNMENT_t)aligned;
                            results.scrambleInfo.has_iv_value = has_iv;
                            if ( has_iv && descriptor_length > 4 + 16 ) {
                                memcpy(results.scrambleInfo.iv_value_data, &p2[7], 16);
                            }
                        }
                    }

                    results.scrambled = true;
                    results.caSystemId = ca_system_id;

                    esStream.ecmPid = ecm_pid;
                }
                break;

                case 0x0A:
                {
                    int count = descriptor_length / 4;
                    const uint8_t* p3 = p2 + 2;
                    while (count-- > 0) {
                        int language_code = p3[0]<<16 | p3[1]<<8 | p[2];
                        int audio_type = p3[3];
                        p3 += 4;

                        MLOGI("language_code:%#x, audio_type:%d", language_code, audio_type);
                        if (audio_type == 0x03) {
                            esStream.isAD = true;
                        }
                    }
                }
                break;

                case 0x56:
                {
                    if (descriptor_length > 0 && descriptor_length % 5 != 0) {
                        break;
                    }
                    int language_count = descriptor_length / 5;
                    for (int i = 0; i < language_count; i++) {
                        int type = p2[i * 5 + 5] >> 3;
                        // type == 2: Teletext subtitle page
                        // type == 5: Teletext subtitle page for hearing impaired people
                        if (type == 2 || type == 5) {
                            // choose the first subtitle magazine
                            esStream.magazine = p2[i * 5 + 5] & 0x07;
                            esStream.page = p2[i * 5 + 6];
                            MLOGI("magazine:%#x, page:%#x", esStream.magazine, esStream.page);
                            break;
                        }
                    }
                    break;
                }

                case 0x59:
                {
                    int language_count = descriptor_length / 8;
                    if (language_count > 0) {
                        esStream.compositionPageId = p2[6] << 8 | p2[7];
                        esStream.ancillaryPageId = p2[7] << 8 | p2[9];
                        MLOGI("compositionPageId:%#x, ancillaryPageId:%#x", esStream.compositionPageId, esStream.ancillaryPageId);
                    }

                }
                break;

                case 0x05:
                {
                    int32_t formatIdentifier = MKTAG(p2[2], p2[3], p2[4], p2[5]);
                    MLOGI("formatIdentifier:%#x, %x %x %x %x", formatIdentifier, p2[2], p2[3], p2[4], p2[5]);
                    esStream.descriptorTags[esStream.descriptorCount-1] = formatIdentifier;
                }
                break;

                case 0x7f:
                {
                    int extDescTag = p2[2];
                    if (extDescTag == 0x15) {
                        MLOGI("found AC4 extDescTag");
                        esStream.descriptorTags[esStream.descriptorCount-1] = extDescTag;
                    } else if (extDescTag == 0x20) {
                        MLOGI("found TTML extDescTag");
                        esStream.descriptorTags[esStream.descriptorCount-1] = extDescTag;
                    }
                }
                break;

                default:
                    //MLOGI("unhandled stream descriptor_tag:%#x, length:%d", descriptor_tag, descriptor_length);
                    break;
                }

                p2 += 2 + descriptor_length;
                descriptorsRemaining -= 2 + descriptor_length;
            }

            p = section.advance(es_info_length);
            infoBytesRemaining -= es_info_length;
        }

        results.streamCount++;
        results.streams.push_back(esStream);
        MLOGE("programNumber:%d, stream pid:%d, type:%#x\n", programNumber, elementary_pid, stream_type);

    }

    if (parser) {
        SectionData sectionData(pid, size, (uint8_t *)data);
        parser->onPmtParsed(sectionData, results);
    }

    return 0;
}


int Parser::catCb(int pid, size_t size, const uint8_t* data, void* userData)
{
    Parser* parser = (Parser*)userData;

    MLOGI("cat cb, size:%zu", size);
    Section section(data, size);
    const uint8_t* p = section.data();

    //int table_id = p[0];
    int section_syntax_indicator = (p[1]&0x80) >> 7;
    AML_MP_CHECK_EQ(section_syntax_indicator, 1);
    int section_length = (p[1] & 0x0F) << 4 | p[2];
    AML_MP_CHECK_LE(section_length, 4093);
    MLOGI("section_length = %d, size:%zu", section_length, size);

    p = section.advance(3);

    //skip section header
    p = section.advance(5);

    CATSection results;
    results.catPid = pid;

    int descriptorsRemaining = section.dataSize() - 4;
    int count = 0;
    while (descriptorsRemaining >= 2) {
        int descriptor_tag = p[0];
        int descriptor_length = p[1];
        switch (descriptor_tag) {
        case 0x09:
        {
            int ca_system_id = p[2]<<8 | p[3];
            int emm_pid = (p[4]&0x1F)<<8 | p[5];
            MLOGI("ca_system_id:%#x, emm_pid:%#x, count:%d, descriptor_length:%d\n", ca_system_id, emm_pid, ++count, descriptor_length);

            results.caSystemId = ca_system_id;
            results.emmPid = emm_pid;
        }
        break;

        default:
            MLOGI("CAT descriptor_tag:%#x", descriptor_tag);
            break;
        }

        p = section.advance(2 + descriptor_length);
        descriptorsRemaining -= 2 + descriptor_length;
    }

    if (descriptorsRemaining != 0) {
        MLOGW("descriptorsRemaining = %d\n", descriptorsRemaining);
    }

    if (parser) {
        SectionData sectionData(pid, size, (uint8_t *)data);
        parser->onCatParsed(sectionData, results);
    }

    return 0;
}

int Parser::ecmCb(int pid, size_t size, const uint8_t* data, void* userData)
{
    Parser* parser = (Parser*)userData;

    MLOGI("ecm cb, pid:0x%04X, size:%zu", pid, size);
    ECMSection results;
    results.ecmPid = pid;
    results.size = size;
    results.data = new uint8_t[size];
    memcpy(results.data, data, size);

    if (parser) {
        parser->onEcmParsed(results);
    }

    delete[] results.data;

    return 0;
}

void Parser::onPatParsed(const PATSection& result)
{
    MLOGI("PATSection parsed: version:%d", result.version_number);
    removeFilter(0);

    int programCount = 0;
    mPidProgramMap.clear();
    bool useFirstProgram = true;
    if (mProgramNumber >= 0 || mVPid != AML_MP_INVALID_PID || mAPid != AML_MP_INVALID_PID) {
        useFirstProgram = false;
    }
    for (auto& p : result.pmtInfos) {
        programCount++;
        mPidProgramMap.emplace(p.pmtPid, p.programNumber);
        if (useFirstProgram && mProgramNumber == -1) {
            mProgramNumber = p.programNumber;
        }
        addSectionFilter(p.pmtPid, pmtCb, this);
    }

    if (programCount == 0) {
        MLOGI("no valid program found!");
        std::lock_guard<std::mutex> _l(mLock);
        notifyParseDone_l();
    }
}

void Parser::onPmtParsed(const SectionData& sectionData, const PMTSection& results)
{
    if (results.streamCount == 0) {
        return;
    }

    bool isProgramSelected = false;
    bool isNewEcm = false;
    bool isNewPmt = false;
    bool isPidChanged = false;
    std::vector<Aml_MP_PlayerEventPidChangeInfo> pidChangeInfos;
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mProgramNumber != -1) {
            // check if this pmt is the selected program
            if (mProgramNumber == results.programNumber) {
                isProgramSelected = true;
            }
        } else {
            // check if this pmt contains with a/v pid
            bool containsAudio = false, containsVideo = false;
            for (PMTStream stream : results.streams) {
                if (mVPid == stream.streamPid) {
                    containsVideo = true;
                } else if (mAPid == stream.streamPid) {
                    containsAudio = true;
                }
            }
            if ((mVPid == AML_MP_INVALID_PID || containsVideo) && (mAPid == AML_MP_INVALID_PID || containsAudio)) {
                isProgramSelected = true;
                mProgramNumber = results.programNumber;
            }
        }

        //check if this pmt is newPmt or pmt changed
        auto it = mPidPmtMap.find(results.pmtPid);
        if (it == mPidPmtMap.end()) {
            // is new pmt
            isNewPmt = true;
            mPidPmtMap.emplace(results.pmtPid, results);
        } else {
            // is pmt changed
            isPidChanged = checkPidChange(it->second, results, &pidChangeInfos);
            mPidPmtMap.erase(results.pmtPid);
            mPidPmtMap.emplace(results.pmtPid, results);
        }

        //check is newEcm
        if (isProgramSelected && results.scrambled && mEcmPidSet.find(results.ecmPid) == mEcmPidSet.end()) {
            isNewEcm = true;
            mEcmPidSet.emplace(results.ecmPid);
        }
    }

    if (!isProgramSelected) {
        MLOGI("not program selected");
        return;
    }

    if (isNewEcm && results.scrambled) {
        // filter ecmData
        addSectionFilter(results.ecmPid, ecmCb, this, false);
    }

    sptr<ProgramInfo> programInfo = mProgramInfo;
    programInfo->programNumber = results.programNumber;
    programInfo->pmtPid = results.pmtPid;
    programInfo->caSystemId = results.caSystemId;
    programInfo->scrambled = results.scrambled;
    programInfo->scrambleInfo = results.scrambleInfo;
    programInfo->serviceIndex = 0;
    programInfo->serviceNum = 0;
    programInfo->ecmPid[ECM_INDEX_AUDIO] = results.ecmPid;
    programInfo->ecmPid[ECM_INDEX_VIDEO] = results.ecmPid;
    programInfo->ecmPid[ECM_INDEX_SUB] = results.ecmPid;
    programInfo->privateDataLength = results.privateDataLength;
    memcpy(programInfo->privateData, results.privateData, results.privateDataLength);

    const struct StreamType* typeInfo;
    for (auto it : results.streams) {
        PMTStream* stream = &it;
        typeInfo = getStreamTypeInfo(stream->streamType);
        if (typeInfo == nullptr) {
            for (int j = 0; j < stream->descriptorCount; ++j) {
                typeInfo = getStreamTypeInfo(stream->descriptorTags[j], g_descTypes);
                if (typeInfo != nullptr) {
                    MLOGI("stream pid:%d, found tag:%#x", stream->streamPid, stream->descriptorTags[j]);
                    break;
                }

                typeInfo = getStreamTypeInfo(stream->descriptorTags[j], g_identifierTypes);
                if (typeInfo != nullptr) {
                    MLOGI("identified stream pid:%d, %.4s", stream->streamPid, (char*)&stream->descriptorTags[j]);
                    break;
                }

                typeInfo = getStreamTypeInfo(stream->descriptorTags[j], g_extDescTypes);
                if (typeInfo != nullptr) {
                    MLOGI("extDescTag match stream pid:%d, tag:%#x", stream->streamPid, stream->descriptorTags[j]);
                    break;
                }
            }
        }

        if (typeInfo == nullptr) {
            continue;
        }

        switch (typeInfo->mpStreamType) {
        case AML_MP_STREAM_TYPE_AUDIO:
        {
            if (stream->isAD) {
                if (programInfo->adPid == AML_MP_INVALID_PID) {
                    programInfo->adPid = stream->streamPid;
                    programInfo->adCodec = typeInfo->codecId;
                }
            } else if (programInfo->audioPid == AML_MP_INVALID_PID) {
                programInfo->audioPid = stream->streamPid;
                programInfo->audioCodec = typeInfo->codecId;
                if (stream->ecmPid != AML_MP_INVALID_PID) {
                    programInfo->ecmPid[ECM_INDEX_AUDIO] = stream->ecmPid;
                }
            }
            StreamInfo streamInfo;
            streamInfo.type = TYPE_AUDIO;
            streamInfo.pid = stream->streamPid;
            streamInfo.codecId = typeInfo->codecId;
            programInfo->audioStreams.push_back(streamInfo);
            MLOGI("audio pid:%d(%#x), codec:%s, isAD:%d", streamInfo.pid, streamInfo.pid, mpCodecId2Str(streamInfo.codecId), stream->isAD);
            break;
        }

        case AML_MP_STREAM_TYPE_VIDEO:
        {
            if (programInfo->videoPid == AML_MP_INVALID_PID) {
                programInfo->videoPid = stream->streamPid;
                programInfo->videoCodec = typeInfo->codecId;
                if (stream->ecmPid != AML_MP_INVALID_PID) {
                    programInfo->ecmPid[ECM_INDEX_VIDEO] = stream->ecmPid;
                }
            }
            StreamInfo streamInfo;
            streamInfo.type = TYPE_VIDEO;
            streamInfo.pid = stream->streamPid;
            streamInfo.codecId = typeInfo->codecId;
            programInfo->videoStreams.push_back(streamInfo);
            MLOGI("video pid:%d(%#x), codec:%s", streamInfo.pid, streamInfo.pid, mpCodecId2Str(streamInfo.codecId));
            break;
        }

        case AML_MP_STREAM_TYPE_SUBTITLE:
        {
            if (programInfo->subtitlePid == AML_MP_INVALID_PID) {
                programInfo->subtitlePid = stream->streamPid;
                programInfo->subtitleCodec = typeInfo->codecId;
                if (stream->ecmPid != AML_MP_INVALID_PID) {
                    programInfo->ecmPid[ECM_INDEX_SUB] = stream->ecmPid;
                }
                if (programInfo->subtitleCodec == AML_MP_SUBTITLE_CODEC_DVB) {
                    programInfo->compositionPageId = stream->compositionPageId;
                    programInfo->ancillaryPageId = stream->ancillaryPageId;
                } else if (programInfo->subtitleCodec == AML_MP_SUBTITLE_CODEC_TELETEXT) {
                    programInfo->magazine = stream->magazine;
                    programInfo->page = stream->page;
                }
            }
            StreamInfo streamInfo;
            streamInfo.type = TYPE_SUBTITLE;
            streamInfo.pid = stream->streamPid;
            streamInfo.codecId = typeInfo->codecId;
            if (streamInfo.codecId == AML_MP_SUBTITLE_CODEC_DVB) {
                streamInfo.compositionPageId = stream->compositionPageId;
                streamInfo.ancillaryPageId = stream->ancillaryPageId;
            } else if (streamInfo.codecId == AML_MP_SUBTITLE_CODEC_TELETEXT) {
                streamInfo.magazine = stream->magazine;
                streamInfo.page = stream->page;
            }
            programInfo->subtitleStreams.push_back(streamInfo);
            MLOGI("subtitle pid:%d(%#x), codec:%s", streamInfo.pid, streamInfo.pid, mpCodecId2Str(streamInfo.codecId));
            break;
        }

        case AML_MP_STREAM_TYPE_AD:
            break;

        default:
            break;
        }
    }

    if (mCb && isProgramSelected) {
        mCb(ProgramEventType::EVENT_PMT_PARSED, sectionData.pid, sectionData.size, (void *)sectionData.data);
    }

    if (isNewPmt) {
        if (mCb && mProgramInfo->isComplete()) {
            mCb(ProgramEventType::EVENT_PROGRAM_PARSED, mProgramInfo->pmtPid, mProgramInfo->programNumber, mProgramInfo.get());
        }
    } else if (isPidChanged) {
        if (mCb) {
            for (Aml_MP_PlayerEventPidChangeInfo pidChangeInfo : pidChangeInfos) {
                mCb(ProgramEventType::EVENT_AV_PID_CHANGED, results.pmtPid, results.programNumber, (void *)&pidChangeInfo);
            }
        }
    }

    if (mProgramInfo->isComplete()) {
        std::lock_guard<std::mutex> _l(mLock);
        notifyParseDone_l();
    }
}

void Parser::onCatParsed(const SectionData& sectionData, const CATSection& results)
{
    mProgramInfo->scrambled = true;
    mProgramInfo->caSystemId = results.caSystemId;
    mProgramInfo->emmPid = results.emmPid;

    if (mCb) {
        mCb(ProgramEventType::EVENT_CAT_PARSED, sectionData.pid, sectionData.size, (void *)sectionData.data);
        {
            std::lock_guard<std::mutex> _l(mLock);
            mCATParseDone = true;
            mCond.notify_all();
        }
        if (mProgramInfo->isComplete()) {
            mCb(ProgramEventType::EVENT_PROGRAM_PARSED, mProgramInfo->pmtPid, mProgramInfo->programNumber, mProgramInfo.get());
        }
    }

    if (mProgramInfo->isComplete()) {
        std::lock_guard<std::mutex> _l(mLock);
        notifyParseDone_l();
    }
}

void Parser::onEcmParsed(const ECMSection& results){
    if (mCb) {
        mCb(ProgramEventType::EVENT_ECM_DATA_PARSED, results.ecmPid, results.size, results.data);
    }
}

/**
 * 1. same pid and same streamType
 * 2. only pid changed
 * 3. only streamType changed
 * 4. pid and streamType both changed (same Aml_MP_StreamType)
 * 5. old stream removed
 * 6. new stream inserted
 */
bool Parser::checkPidChange(const PMTSection& oldPmt, const PMTSection& newPmt, std::vector<Aml_MP_PlayerEventPidChangeInfo> *pidChangeInfos)
{
    std::map<int, PMTStream> oldPidStreamMap;
    std::map<int, PMTStream> newPidStreamMap;
    std::set<int> unChangedPidSet; // case 1: same pid and same streamType
    std::set<std::pair<int, int>> streamChangedSet; // case 2/3/4/5/6: oldPid --> newPid

    for (PMTStream pmtStream : oldPmt.streams) {
        oldPidStreamMap.insert({pmtStream.streamPid, pmtStream});
    }
    for (PMTStream pmtStream : newPmt.streams) {
        newPidStreamMap.insert({pmtStream.streamPid, pmtStream});
    }
    // find all streams in case 1, 3
    for (auto oldPidStream : oldPidStreamMap) {
        int oldPid = oldPidStream.first;
        auto newPidStream = newPidStreamMap.find(oldPid);
        if (newPidStream != newPidStreamMap.end()) {
            if (newPidStream->second.streamType == oldPidStream.second.streamType) {
                unChangedPidSet.insert(oldPid); // 1. same pid and same streamType
            } else {
                streamChangedSet.insert(std::pair<int,int>(oldPid, newPidStream->first)); // 3. only streamType changed
            }
        }
    }

    // remove all streams in case 1
    for (int pid : unChangedPidSet) {
        oldPidStreamMap.erase(pid);
        newPidStreamMap.erase(pid);
    }
    // remove all streams in case 3
    for (auto streamChanged : streamChangedSet) {
        oldPidStreamMap.erase(streamChanged.first);
        newPidStreamMap.erase(streamChanged.second);
    }
    // find all streams in case 2, 4, 5
    for (auto oldPidStream : oldPidStreamMap) {
        int oldPid = oldPidStream.first;
        int streamType = oldPidStream.second.streamType;
        bool isStreamRemoved = true;
        for (auto newPidStream : newPidStreamMap) {
            if (newPidStream.second.streamType == streamType) {
                streamChangedSet.insert(std::pair<int,int>(oldPid, newPidStream.first)); // 2. only pid changed
                newPidStreamMap.erase(newPidStream.first);
                isStreamRemoved = false;
                break;
            }
            Aml_MP_StreamType oldType = getStreamTypeInfo(oldPidStream.second.streamType)->mpStreamType;
            Aml_MP_StreamType newType = getStreamTypeInfo(newPidStream.second.streamType)->mpStreamType;
            if (oldType == newType) {
                streamChangedSet.insert(std::pair<int,int>(oldPid, newPidStream.first)); // 4. pid and streamType both changed (same Aml_MP_StreamType)
                newPidStreamMap.erase(newPidStream.first);
                isStreamRemoved = false;
                break;
            }
        }
        if (isStreamRemoved) {
            streamChangedSet.insert(std::pair<int,int>(oldPid, AML_MP_INVALID_PID)); // 5. old stream removed
        }
    }
    // remove all streams in case 2, 4, 5
    for (auto streamChanged : streamChangedSet) {
        if (streamChanged.first != AML_MP_INVALID_PID) {
            oldPidStreamMap.erase(streamChanged.first);
        }
        if (streamChanged.second != AML_MP_INVALID_PID) {
            newPidStreamMap.erase(streamChanged.second);
        }
    }
    // find all streams in case 6
    for (auto newPidStream : newPidStreamMap) {
        streamChangedSet.insert(std::pair<int,int>(AML_MP_INVALID_PID, newPidStream.first)); // 6. new stream inserted
    }

    // convert streamChangedSet to Aml_MP_PlayerEventPidChangeInfos
    bool isPidChange = !streamChangedSet.empty();
    oldPidStreamMap.clear();
    newPidStreamMap.clear();
    for (PMTStream pmtStream : oldPmt.streams) {
        oldPidStreamMap.insert({pmtStream.streamPid, pmtStream});
    }
    for (PMTStream pmtStream : newPmt.streams) {
        newPidStreamMap.insert({pmtStream.streamPid, pmtStream});
    }
    for (auto streamChanged : streamChangedSet) {
        Aml_MP_PlayerEventPidChangeInfo pidChangeInfo;
        memset(&pidChangeInfo, 0, sizeof(Aml_MP_PlayerEventPidChangeInfo));
        pidChangeInfo.programNumber = newPmt.programNumber;
        pidChangeInfo.programPid = newPmt.pmtPid;
        pidChangeInfo.oldStreamPid = streamChanged.first;
        pidChangeInfo.newStreamPid = streamChanged.second;
        if (pidChangeInfo.newStreamPid != AML_MP_INVALID_PID) {
            PMTStream newPmtStream = newPidStreamMap.find(pidChangeInfo.newStreamPid)->second;
            const struct StreamType* type = getStreamTypeInfo(newPmtStream.streamType);
            pidChangeInfo.type = type->mpStreamType;
            switch (pidChangeInfo.type) {
                case AML_MP_STREAM_TYPE_AUDIO:
                    pidChangeInfo.u.audioParams.pid = streamChanged.second;
                    pidChangeInfo.u.audioParams.audioCodec = type->codecId;
                    break;
                case AML_MP_STREAM_TYPE_VIDEO:
                    pidChangeInfo.u.videoParams.pid = streamChanged.second;
                    pidChangeInfo.u.videoParams.videoCodec = type->codecId;
                    break;
                case AML_MP_STREAM_TYPE_SUBTITLE:
                    pidChangeInfo.u.subtitleParams.pid = streamChanged.second;
                    pidChangeInfo.u.subtitleParams.subtitleCodec = type->codecId;
                    break;
                default:
                    break;
            }
        }
        pidChangeInfos->push_back(pidChangeInfo);
    }
    return isPidChange;
}

///////////////////////////////////////////////////////////////////////////////
int Parser::addFilter(int pid, Aml_MP_Demux_FilterCb cb, void* userData, const Aml_MP_DemuxFilterParams* params)
{
    int ret = 0;
    if (mFilters.find(pid) != mFilters.end()) {
        MLOGW("addFilter repeatedly, pid:%d", pid);
        return -1;
    }

    sptr<FilterContext> context = new FilterContext(pid);
    if (context == nullptr) {
        return -1;
    }

    context->channel = mDemux->createChannel(pid, params);
    ret = mDemux->openChannel(context->channel);
    if (ret < 0) {
        MLOGE("open channel pid:%d failed!", pid);
        return ret;
    }

    context->filter = mDemux->createFilter(cb, userData);
    if (ret < 0) {
        MLOGE("create filter for pid:%d failed!", pid);
        return ret;
    }

    ret = mDemux->attachFilter(context->filter, context->channel);
    if (ret < 0) {
        MLOGE("attach filter for pid:%d failed!", pid);
        return ret;
    }

    {
        std::lock_guard<std::mutex> _l(mLock);
        mFilters.emplace(pid, context);
    }

    return ret;
}

int Parser::removeFilter(int pid)
{
    std::lock_guard<std::mutex> _l(mLock);
    sptr<FilterContext> context;

    auto it = mFilters.find(pid);
    if (it == mFilters.end()) {
        return 0;
    }
    context = it->second;

    if (context->filter != AML_MP_INVALID_HANDLE) {
        mDemux->detachFilter(context->filter, context->channel);
        mDemux->destroyFilter(context->filter);
        context->filter = AML_MP_INVALID_HANDLE;
    }

    if (context->channel != AML_MP_INVALID_HANDLE) {
        mDemux->closeChannel(context->channel);
        mDemux->destroyChannel(context->channel);
        context->channel = AML_MP_INVALID_HANDLE;
    }

    int ret = mFilters.erase(context->mPid);
    MLOGI("pid:%d, %d sections removed!", pid, ret);

    return 0;
}

void Parser::clearAllFilters()
{
    std::lock_guard<std::mutex> _l(mLock);
    for (auto p : mFilters) {
        sptr<FilterContext> context = p.second;
        if (context->filter != AML_MP_INVALID_HANDLE) {
            mDemux->detachFilter(context->filter, context->channel);
            mDemux->destroyFilter(context->filter);
            context->filter = AML_MP_INVALID_HANDLE;
        }

        if (context->channel != AML_MP_INVALID_HANDLE) {
            mDemux->closeChannel(context->channel);
            mDemux->destroyChannel(context->channel);
            context->channel = AML_MP_INVALID_HANDLE;
        }
    }

    mFilters.clear();
}

void Parser::notifyParseDone_l()
{
    mParseDone = true;
    mCond.notify_all();
}

////////////////////////////////////////////////////////////////////////////////
#define TS_PACKET_SIZE 188

size_t findEcmPacket(const uint8_t* buffer, size_t size, const std::vector<int>& ecmPids, size_t* ecmSize)
{
    size_t offset = 0;
    int pid = -1;

    for (; offset <= size-TS_PACKET_SIZE; ) {
        if (buffer[offset] != 0x47 || buffer[offset+TS_PACKET_SIZE] != 0x47) {
            ++offset;
            continue;
        }

        pid = (buffer[offset+1]<<8 | buffer[offset+2]) & 0x1FFF;
        if (pid == 0 || pid == 0x1FFF) {
            offset += TS_PACKET_SIZE;
            continue;
        }

        for (size_t i = 0; i < ecmPids.size(); ++i) {
            if (pid == ecmPids[i]) {
                *ecmSize = TS_PACKET_SIZE;
                return offset;
            }
        }
        offset += TS_PACKET_SIZE;
    }

    *ecmSize = 0;
    return size;
}


}
