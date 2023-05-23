/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_MP_LOG_H_
#define _AML_MP_LOG_H_

#ifdef __linux__
#ifndef ANDROID

#ifndef LOG_NDEBUG
    #define LOG_NDEBUG 1
#endif

#endif
#endif

#include <cutils/log.h>
#include "AmlMpConfig.h"

#define AML_MP_LOG_TAG  "Aml_MP"

#if LOG_NDEBUG
#define MLOGV(fmt, ...)
#else
#define MLOGV(fmt, ...) ALOG(LOG_VERBOSE, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__)
#endif

#define MLOGD(fmt, ...) AmlMpConfig::instance().mLogDebug?ALOG(LOG_DEBUG, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__):0
#define MLOGI(fmt, ...) ALOG(LOG_INFO, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__)
#define MLOGW(fmt, ...) ALOG(LOG_WARN, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__)
#define MLOGE(fmt, ...) ALOG(LOG_ERROR, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__)
#define MLOGI_IF(cond, fmt, ...) \
({do { \
    if ((cond)) \
        (void)ALOG(LOG_INFO, AML_MP_LOG_TAG, "%s " fmt, mName, ##__VA_ARGS__); \
} while(0);})


#ifndef MLOG
#define MLOG(fmt, ...) MLOGI("[%s:%d] " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#endif
