/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#define LOG_TAG "AmlMpCodecCapability"
#include "AmlMpUtils.h"
#include "AmlMpCodecCapability.h"
#include "Amlsysfsutils.h"
#include "AmlMpLog.h"

#define VIDEO_SUPPORT_INFO_PATH "/sys/class/amstream/vcodec_profile"

namespace aml_mp {

static const char* mName = LOG_TAG;

static const DecoderNamePair mDecoderNameMap[] = {
    {AML_MP_VIDEO_CODEC_H264,   "mh264"},
    {AML_MP_VIDEO_CODEC_HEVC,   "mh265"},
    {AML_MP_VIDEO_CODEC_MJPEG,  "mmjpeg"},
    {AML_MP_VIDEO_CODEC_MPEG12, "mmpeg12"},
    {AML_MP_VIDEO_CODEC_MPEG4,  "mmpeg4"},
    {AML_MP_VIDEO_CODEC_VP9,    "mvp9"},
    {AML_MP_VIDEO_CODEC_AVS2,   "mavs2"},
    {AML_MP_VIDEO_CODEC_AVS,    "mavs"},
    {AML_MP_VIDEO_CODEC_AV1,    "mav1"},
};

static const ResolutionNamePair mResolutionMap[] = {
    {AML_MP_RESOLUTION_8K,      "8k"},
    {AML_MP_RESOLUTION_4K,      "4k"},
    {AML_MP_RESOLUTION_1080P,   "1080p"},
};

AmlMpCodecCapability::AmlMpCodecCapability()
{
    updateVideoCapability();
    updateAudioCapability();
}

AmlMpCodecCapability::~AmlMpCodecCapability()
{
    MLOGI();
}

AmlMpCodecCapability* AmlMpCodecCapability::getCodecCapabilityHandle() {
    static AmlMpCodecCapability instance;
    return &instance;
}

bool AmlMpCodecCapability::matchVideoCodecType(std::string str, Aml_MP_CodecID* codecId) {
    for (DecoderNamePair testPair : mDecoderNameMap) {
        if (testPair.decoderNameStr == str) {
            *codecId = testPair.decoderName;
            return true;
        }
    }
    return false;
}

bool AmlMpCodecCapability::matchMaxResolution(std::string str, Aml_MP_Resolution* resolution) {
    /* Not set default max resolution */
    *resolution = AML_MP_RESOLUTION_MAX;

    for (ResolutionNamePair testPair : mResolutionMap) {
        if (testPair.resolutionStr == str) {
            *resolution = testPair.maxResolution;
            return true;
        }
    }

    return true;
}

void AmlMpCodecCapability::getVideoDecoderCapability(std::string str){
    std::vector<std::string> splitInfo;
    Aml_MP_CodecID decoderName;

    split(str, splitInfo, " ,:;");

    if (matchVideoCodecType(splitInfo[0], &decoderName)) {
        Aml_MP_DecoderCapabilityInfo info;
        info.decoderName = decoderName;
        if (splitInfo.size() > 1) {
            matchMaxResolution(splitInfo[1], &(info.decoderMaxResolution));
        } else {
            info.decoderMaxResolution = AML_MP_RESOLUTION_MAX;
        }
        mVideoCapabilityMap.push_back(info);
    }
}

void AmlMpCodecCapability::updateVideoCapability() {
    char videoSupportInfoStr[2000];
    std::string strOfInfo;
    std::vector<std::string> InfoByLine;

    amsysfs_get_sysfs_str(VIDEO_SUPPORT_INFO_PATH, videoSupportInfoStr, sizeof(videoSupportInfoStr));
    strOfInfo = videoSupportInfoStr;

    split(strOfInfo, InfoByLine, "\n");

    for (std::string tmp : InfoByLine) {
        getVideoDecoderCapability(tmp);
    }
}

void AmlMpCodecCapability::updateAudioCapability() {
    int ret = 0;
    for (int i = AML_MP_AUDIO_CODEC_BASE + 1; i < AML_MP_AUDIO_CODEC_MAX; i++) {
        //TODO: This function need media_hal merge, after media_hal code merge need open this func to enable audio capability info get.
#if 0
        ret = AmTsPlayer_isAudioCodecSupport(convertToAudioCodec((Aml_MP_CodecID)i));

        if (ret > 0) {
            Aml_MP_DecoderCapabilityInfo info;
            info.decoderName = (Aml_MP_CodecID)i;
            mAudioCapabilityMap.push_back(info);
        }
#endif
    }
}

void AmlMpCodecCapability::getCodecCapabilityStr(Aml_MP_CodecID codecId, char* caps, size_t size) {
    *caps = '\0';
    Json::Value tmpJson;
    std::string tmpString;
    Json::StreamWriterBuilder builder;
    const char *pstr = NULL;

    if (codecId > AML_MP_VIDEO_CODEC_BASE && codecId < AML_MP_VIDEO_CODEC_MAX) {
        for (Aml_MP_DecoderCapabilityInfo tmp: mVideoCapabilityMap) {
            if (codecId == tmp.decoderName) {
                tmpJson["codecName"] = convertToMIMEString(codecId);
                pstr = convertToResolutionString(tmp.decoderMaxResolution);
                if (pstr) {
                    tmpJson["maxResolution"] = pstr;
                }
                tmpString = Json::writeString(builder, tmpJson);
                memcpy(caps, tmpString.c_str(), tmpString.length()+1 < size ? tmpString.length()+1 : size);
                break;
            }
        }
    } else if (codecId > AML_MP_AUDIO_CODEC_BASE && codecId < AML_MP_AUDIO_CODEC_MAX) {
        for (Aml_MP_DecoderCapabilityInfo tmp: mAudioCapabilityMap) {
            if (codecId == tmp.decoderName) {
                tmpJson["codecName"] = convertToMIMEString(codecId);
                tmpString = Json::writeString(builder, tmpJson);
                memcpy(caps, tmpString.c_str(), tmpString.length()+1 < size ? tmpString.length()+1 : size);
                break;
            }
        }
    }
}

const char resolutionMap[][10] = {"1920*1080", "3840*2160", "7680*4320",};

const char* AmlMpCodecCapability::convertToResolutionString(Aml_MP_Resolution resolution) {
    if (resolution < AML_MP_RESOLUTION_MAX) {
        return resolutionMap[resolution];
    }
    return NULL;
}

}
