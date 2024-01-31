/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_NDEBUG 0
#define LOG_TAG "AmlMpPlayerDemo"
#include <utils/AmlMpLog.h>
#include <AmlMpTestSupporter.h>
#include <unistd.h>
#include <getopt.h>
#include "TestUtils.h"

static const char* mName = LOG_TAG;
using namespace aml_mp;

struct Argument
{
    std::string url;
    int x = 0;
    int y = 0;
    int viewWidth = -1;
    int viewHeight = -1;
    int playMode = 0;
    int videoMode = 0;
    int zorder = 0;
    int record = 0;
    int crypto = 0;
    int uiMode = 0;
    int channelId = -1;
    int videoErrorRecoveryMode = 0;
    uint64_t options = 0;
    bool esMode = false;
    bool clearTVP = false;
    bool timeShift = false;
    bool appendMode = false;
};

static int parseCommandArgs(int argc, char* argv[], Argument* argument)
{
    static const struct option longopts[] = {
        {"help",        no_argument,        nullptr, 'h'},
        {"size",        required_argument,  nullptr, 's'},
        {"pos",         required_argument,  nullptr, 'p'},
        {"playmode",    required_argument,  nullptr, 'm'},
        {"zorder",      required_argument,  nullptr, 'z'},
        {"videomode",   required_argument,  nullptr, 'v'},
        {"record",      no_argument,        nullptr, 'r'},
        {"crypto",      no_argument,        nullptr, 'c'},
        {"ui",          no_argument,        nullptr, 'u'},
        {"id",          required_argument,  nullptr, 'd'},
        {"video-error-recovery-mode",    required_argument,  nullptr, 'e'},
        {"options",     required_argument,  nullptr, 'o'},
        {"esmode",      no_argument,        nullptr, 'esmd'},
        {"cleartvp",    no_argument,        nullptr, 't'},
        {"timeshift",   no_argument,        nullptr, 'tf'},
        {"appendmode",  no_argument,        nullptr, 'am'},
        {nullptr,       no_argument,        nullptr, 0},
    };

    int opt, longindex;
    while ((opt = getopt_long(argc, argv, "", longopts, &longindex)) != -1) {
        //printf("opt = %d\n", opt);
        switch (opt) {
        case 's':
            {
                int width, height;
                if (sscanf(optarg, "%dx%d", &width, &height) != 2) {
                    printf("parse %s failed! %s\n", longopts[longindex].name, optarg);
                } else {
                    argument->viewWidth = width;
                    argument->viewHeight = height;
                }
            }
            break;

        case 'p':
            {
                int x, y;
                //printf("optarg:%s\n", optarg);
                if (sscanf(optarg, "%dx%d", &x, &y) != 2) {
                    printf("parse %s failed! %s\n", longopts[longindex].name, optarg);
                } else {
                    argument->x = x;
                    argument->y = y;
                    printf("pos:%dx%d\n", x, y);
                }
            }
            break;

        case 'm':
            {
                int playMode = strtol(optarg, nullptr, 0);
                if (playMode < 0 || playMode >= AmlMpTestSupporter::PLAY_MODE_MAX) {
                    playMode = 0;
                }
                printf("playMode:%d\n", playMode);
                argument->playMode = playMode;
            }
            break;

        case 'z':
        {
            int zorder = strtol(optarg, nullptr, 0);
            printf("zorder:%d\n", zorder);
            argument->zorder = zorder;
        }
        break;

        case 'v':
        {
            int videoMode = strtol(optarg, nullptr, 0);
            printf("videoMode:%d\n", videoMode);
            argument->videoMode = videoMode;
        }
        break;

        case 'r':
        {
            printf("record mode!\n");
            argument->record = true;
        }
        break;

        case 'c':
        {
            printf("crypto mode!\n");
            argument->crypto = true;
        }
        break;

        case 'u':
        {
            printf("ui mode!\n");
            argument->uiMode = true;
        }
        break;

        case 'd':
        {
            int channelId = strtol(optarg, nullptr, 0);
            printf("channel id:%d\n", channelId);
            argument->channelId = channelId;
        }
        break;

        case 'e':
        {
            int tmpMode = strtol(optarg, nullptr, 0);
            printf("video error recovery mode: %d(%s)\n", tmpMode, mpVideoErrorRecoveryMode2Str((Aml_MP_VideoErrorRecoveryMode)tmpMode));
            argument->videoErrorRecoveryMode = tmpMode;
        }
        break;

        case 'o':
        {
            uint64_t options = strtoul(optarg, nullptr, 0);
            printf("options :0x%" PRIx64 "\n", options);
            argument->options = options;
        }
        break;

        case 'esmd':
        {
            printf("esmode!\n");
            argument->esMode = true;
        }
        break;


        case 't':
        {
            printf("clearTVP\n");
            argument->clearTVP = true;
        }
        break;

        case 'tf':
        {
            printf("timeShift\n");
            argument->timeShift = true;
        }
        break;

        case 'am':
        {
            printf("appendmode\n");
            argument->appendMode = true;
        }
        break;

        case 'h':
        default:
            return -1;
        }
    }

    //printf("optind:%d, argc:%d\n", optind, argc);
    if (optind < argc) {
        argument->url = argv[argc-1];
        printf("url:%s\n", argument->url.c_str());
    }

    if (argument->uiMode || !argument->url.empty()) {
        return 0;
    }

    return -1;
}

static void showUsage()
{
    printf("Usage: amlMpPlayerDemo <options> <url>\n"
            "options:\n"
            "    --size:      eg: 1920x1080\n"
            "    --pos:       eg: 0x0\n"
            "    --zorder:    eg: 0\n"
            "    --videomode: 0: set ANativeWindow\n"
            "                 1: set video window\n"
            "    --playmode:  0: set audio param, set video param, start --> stop\n"
            "                 1: set audio param, set video param, start --> stop audio, stop video\n"
            "                 2: set audio param, set video param, start audio, start video --> stop\n"
            "                 3: set audio param, set video param, start audio, start video --> stop audio, stop video\n"
            "                 4: set audio param, set video param, start video, start audio --> stop audio, stop video\n"
            "                 5: set audio param, start audio, set video param, start video --> stop audio, stop video\n"
            "                 6: set video param, start video, set audio param, start audio --> stop video, stop audio\n"
            "   --record:     record mode, record file name is \"" AML_MP_TEST_SUPPORTER_RECORD_FILE "\"\n"
            "   --crypto      crypto mode\n"
            "   --ui          create ui only\n"
            "   --id          specify the corresponding ui channel id\n"
            "   --video-error-recovery-mode\n"
            "                 0: default\n"
            "                 1: drop error frame\n"
            "                 2: do nothing, display error frame\n"
            "   --options     set options, eg: 3, 3 equals with 0b0011, so it means \"prefer tunerhal\" and \"monitor pid change\"\n"
            "                 0-bit set 1 means prefer tunerhal:       AML_MP_OPTION_PREFER_TUNER_HAL\n"
            "                 1-bit set 1 means monitor pid change:    AML_MP_OPTION_MONITOR_PID_CHANGE\n"
            "                 2-bit set 1 means prefer CTC:            AML_MP_OPTION_PREFER_CTC\n"
            "   --esmode      enable es mode playback\n"
#ifdef HAVE_SECMEM
            "   --cleartvp    enable cleartvp for es mode playback\n"
#endif
            "   --timeshift   enable timeshift\n"
            "   --appendmode  enable appendmode to save record file with same location for dvr\n"
            "\n"
            "url format: url?program=xx&demuxid=xx&sourceid=xx&fendid=xx\n"
            "    DVB-T dvbt://<freq>/<bandwidth>, eg: dvbt://474/8M\n"
            "    DVB-C dvbc://<freq>/<symbol rate>/<modulation>, eg: dvbc://474/6900/64qam\n"
            "    DVB-S dvbs://<freq>/<symbol rate>, eg: dvbs://1804/29950\n"
            "    local file, eg: /data/a.ts\n"
            "    dvr replay, eg: dvr:/" AML_MP_TEST_SUPPORTER_RECORD_FILE "\n"
            "    udp source, eg: udp://224.0.0.1:8000\n"
            "    rtp source, eg: rtp://224.0.0.1:8000\n"
            "\n"
            );
}

int startPlayback(const sptr<AmlMpTestSupporter>& mpTestSupporter, Argument& argument, const std::string* url = nullptr)
{
    int ret = 0;
    const std::string& playUrl = url ? *url : argument.url;

    mpTestSupporter->setDataSource(playUrl);
    ret = mpTestSupporter->prepare(argument.crypto);
    if (ret < 0) {
        printf("prepare failed!\n");
        return -1;
    }

    MLOGI(">>>> AmlMpPlayerDemo Start\n");
    mpTestSupporter->setVideoErrorRecoveryMode(argument.videoErrorRecoveryMode);
    mpTestSupporter->setSourceMode(argument.esMode, argument.clearTVP);
    ret = mpTestSupporter->startPlay((AmlMpTestSupporter::PlayMode)argument.playMode, true, true, argument.timeShift);
    if (ret < 0) {
        return -1;
    }
    MLOGI("<<<< AmlMpPlayerDemo Start\n");
    return ret;
}

int main(int argc, char *argv[])
{
    Argument argument{};
    int ret;

    if (parseCommandArgs(argc, argv, &argument) < 0) {
        showUsage();
        return 0;
    }

    sptr<AmlMpTestSupporter> mpTestSupporter = new AmlMpTestSupporter;
    sptr<AmlMpTestSupporter> mpTestSupporter2 = nullptr;
    mpTestSupporter->installSignalHandler();

    AmlMpTestSupporter::DisplayParam displayParam;
    displayParam.x = argument.x;
    displayParam.y = argument.y;
    displayParam.width = argument.viewWidth;
    displayParam.height = argument.viewHeight;
    displayParam.zorder = argument.zorder;
    displayParam.videoMode = argument.videoMode;
    displayParam.channelId = argument.channelId;

    mpTestSupporter->setDisplayParam(displayParam);
    if (argument.options) {
        mpTestSupporter->addOptions(argument.options);
    }

    if (argument.uiMode) {
        mpTestSupporter->startUIOnly();
    } else {
        MLOGI("argument.record=%d argument.timeShift=%d\n", argument.record, argument.timeShift);
        if (argument.record || argument.timeShift) {
            mpTestSupporter->setDataSource(argument.url);
            ret = mpTestSupporter->prepare(argument.crypto);
            if (ret < 0) {
                printf("prepare failed!\n");
                return -1;
            }
            ret = mpTestSupporter->startRecord(true, argument.timeShift, argument.appendMode);
            if (ret < 0) {
                printf("startRecord failed!\n");
                return -1;
            }
            if (argument.timeShift) {
                MLOGI(">>>> TimeshiftRecord START\n");
                sleep(2);//wait for 2s data
                char recordPathInfo[100];
                mpTestSupporter2 = new AmlMpTestSupporter;
                mpTestSupporter2->setDisplayParam(displayParam);
                if (argument.options) {
                    mpTestSupporter2->addOptions(argument.options);
                }
                sprintf(recordPathInfo, "dvr:/%s?demuxid=%d&sourceid=%d", AML_MP_TEST_SUPPORTER_RECORD_FILE,
                       mpTestSupporter->mDemuxId + 1, mpTestSupporter->mDemuxId + 1);
                std::string finalUrl = recordPathInfo;
                ret = startPlayback(mpTestSupporter2, argument, &finalUrl);
                if (ret < 0) {
                    printf("mpTestSupporter2 startDVRPlayback failed!\n");
                    return -1;
                }
                MLOGI("<<<< TimeshiftRecord START\n");
            }
        }else {
            ret = startPlayback(mpTestSupporter, argument);
            if (ret < 0) {
                printf("AmlMpPlayerDemo Start failed!\n");
            }
        }
    }
    mpTestSupporter->fetchAndProcessCommands();
    if (mpTestSupporter2 != nullptr) {
        MLOGI("timeShift mpTestSupporter2 destroy\n");
        mpTestSupporter2->stop();
        mpTestSupporter2.clear();
    }
    mpTestSupporter->stop();
    mpTestSupporter.clear();

    MLOGI("exited!");
    return 0;
}
