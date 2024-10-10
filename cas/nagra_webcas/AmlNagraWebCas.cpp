/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlNagraWebCas"
#include <utils/AmlMpLog.h>
#include <utils/AmlMpUtils.h>
#include "AmlNagraWebCas.h"
#include <dlfcn.h>
#include <cutils/properties.h>
#include <utils/Amlsysfsutils.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

static const char* mName = LOG_TAG;

namespace aml_mp {

AmlNagraWebCas::AmlNagraWebCas(Aml_MP_CASServiceType serviceType)
:AmlCasBase(serviceType)
{
    MLOGI("ctor AmlNagraWebCas");
    pIptvCas = new AmCasLibWrapper<AML_MP_CAS_SERVICE_NAGRA_WEB>("libnagra_drm_plugin.so");
    memset(sessionId, 0, sizeof(sessionId));
}

AmlNagraWebCas::~AmlNagraWebCas()
{
    MLOGI("dtor AmlNagraWebCas");
    int ret = 0;

    pIptvCas.clear();
}

int AmlNagraWebCas::startDescrambling(const Aml_MP_IptvCASParams* params)
{
    MLOG();

    AmlCasBase::startDescrambling(params);

    int ret = 0;
    drmserverinfo_t initParam;
    memset(&initParam, 0, sizeof(initParam));
    initParam.enablelog = 1;
    initParam.serveraddr = mIptvCasParam.serverAddress;
    snprintf(mServerPort, sizeof(mServerPort), "%d", mIptvCasParam.serverPort);
    initParam.serverport = mServerPort;
    initParam.storepath = mIptvCasParam.keyPath;
    ret = pIptvCas->setPrivateData((void *)&initParam, sizeof(drmserverinfo_t));

    ret = pIptvCas->provision();

    if (pIptvCas) {
        MLOGI("%s, vpid=0x%x, apid=0x%x", __func__, mIptvCasParam.videoPid, mIptvCasParam.audioPid);
        pIptvCas->setPids(mIptvCasParam.videoPid, mIptvCasParam.audioPid);
        ret = pIptvCas->openSession(&sessionId[0]);
    }

    return ret;
}

int AmlNagraWebCas::stopDescrambling()
{
    MLOGI("closeSession");
    int ret = 0;

    if (pIptvCas) {
        ret = pIptvCas->closeSession(&sessionId[0]);
        pIptvCas.clear();
    }

    return ret;
}

int AmlNagraWebCas::setPrivateData(const uint8_t* data, size_t size)
{
    int ret = 0;

    if (pIptvCas) {
        uint8_t *pdata = const_cast<uint8_t *>(data);
        ret = pIptvCas->setPrivateData((void*)pdata, size);
        if (ret != 0) {
            MLOGI("setPrivateData failed, ret =%d", ret);
            return ret;
        }
    }

    return ret;
}

int AmlNagraWebCas::processEcm(bool isSection, int ecmPid, const uint8_t* data, size_t size)
{
    AML_MP_UNUSED(isSection);
    AML_MP_UNUSED(ecmPid);
    AML_MP_UNUSED(data);
    AML_MP_UNUSED(size);

    int ret = 0;
    return ret;
}

int AmlNagraWebCas::decrypt(uint8_t *in, int size, void *ext_data, Aml_MP_Buffer* outbuffer)
{
    outbuffer->address = pIptvCas->getOutbuffer();
    int ret = pIptvCas->decrypt(in, outbuffer->address, size, ext_data);
    if (ret) {
        MLOGE("decrypt failed, ret=%d", ret);
        return ret;
    }

    outbuffer->size = size / 188 * 188;
    if (!outbuffer->size || !outbuffer->address) {
        MLOGE("decrypt failed, size=%d, address:%p", ret, outbuffer->address);
        return -1;
    }
    return 0;
}

}
