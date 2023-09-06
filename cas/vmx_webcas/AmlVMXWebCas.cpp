/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlVMXWebCas"
#include <utils/AmlMpLog.h>
#include <utils/AmlMpUtils.h>
#include "AmlVMXWebCas.h"
#include <dlfcn.h>
#include <cutils/properties.h>
#include <utils/Amlsysfsutils.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

static const char* mName = LOG_TAG;


namespace aml_mp {

AmlVMXWebCas::AmlVMXWebCas(Aml_MP_CASServiceType serviceType)
:AmlCasBase(serviceType)
{
    MLOGI("ctor AmlVMXWebCas");
    pIptvCas = new AmCasLibWrapper<AML_MP_CAS_SERVICE_VERIMATRIX_WEB>("libdec_ca_vmx_web.so");
    memset(sessionId, 0, sizeof(sessionId));

    mInstanceId = 0;
    mFirstEcm = 0;
}

AmlVMXWebCas::~AmlVMXWebCas()
{
    MLOGI("dtor AmlVMXWebCas");
    int ret = 0;

    pIptvCas.clear();
}

int AmlVMXWebCas::startDescrambling(const Aml_MP_IptvCASParams* params)
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
        setDscSource(TSN_IPTV);
        MLOGI("%s, vpid=0x%x, apid=0x%x", __func__, mIptvCasParam.videoPid, mIptvCasParam.audioPid);
        pIptvCas->setPids(mIptvCasParam.videoPid, mIptvCasParam.audioPid);
        ret = pIptvCas->openSession(&sessionId[0]);
    }

    return ret;
}

int AmlVMXWebCas::stopDescrambling()
{
    MLOGI("closeSession");
    int ret = 0;

    setDscSource(TSN_DVB);

    if (pIptvCas) {
        ret = pIptvCas->closeSession(&sessionId[0]);
        pIptvCas.clear();
    }

    return ret;
}

int AmlVMXWebCas::setPrivateData(const uint8_t* data, size_t size)
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

int AmlVMXWebCas::processEcm(bool isSection, int ecmPid, const uint8_t* data, size_t size)
{
    AML_MP_UNUSED(isSection);
    AML_MP_UNUSED(ecmPid);
    AML_MP_UNUSED(data);
    AML_MP_UNUSED(size);

    int ret = 0;
    return ret;
}

int AmlVMXWebCas::processEmm(const uint8_t* data, size_t size)
{
    AML_MP_UNUSED(data);
    AML_MP_UNUSED(size);

    int ret = 0;
    return ret;
}

int AmlVMXWebCas::decrypt(uint8_t *in, int size, void *ext_data, Aml_MP_Buffer* outbuffer)
{
    int ret = pIptvCas->decrypt(in, in, size, ext_data);
    if (ret) {
        MLOGE("decrypt failed, ret=%d", ret);
        return ret;
    }
    outbuffer->address = pIptvCas->getOutbuffer();
    outbuffer->size = size / 188 * 188;
    if (!outbuffer->size || !outbuffer->address) {
        return -1;
    }
    return 0;
}

int AmlVMXWebCas::dscDevOpen(const char *port_addr, int flags)
{
    int r;
#if 1
    int retry_open_times = 0;
retry_open:
    r = open(port_addr, flags);
    if (r < 0 /*&& r == EBUSY */)
    {
        retry_open_times++;
        usleep(10*1000);
        if (retry_open_times < 100)
        {
            goto retry_open;
        }
        MLOGI("retry_open [%s] failed,ret = %d error=%d used_times=%d*10(ms)\n", port_addr, r, errno, retry_open_times);
        return r;
    }
    if (retry_open_times > 0)
    {
        MLOGI("retry_open [%s] success\n", port_addr);
    }
#endif
    return (int)r;
}


int AmlVMXWebCas::setDscSource(const char* source)
{
    int ret = 0;
    bool use_hw_multi_demux = false;
#if 1
    if (isSupportMultiHwDemux()) {
        MLOGI("Work with Hw Multi Demux.");
        use_hw_multi_demux = true;
    } else {
        MLOGI("Work with Hw Demux.");
        use_hw_multi_demux = false;
    }
#if 1
    if (!use_hw_multi_demux) {
        ret = amsysfs_set_sysfs_str(DMX0_SOURCE_PATH, DMX_SRC);
        if (ret)
            MLOGI("Error ret 0x%x\n", ret);
        ret = amsysfs_set_sysfs_str(DSC0_SOURCE_PATH, DSC_SRC);
        if (ret)
            MLOGI("Error ret 0x%x\n", ret);
    }
    else
    {
        ret = amsysfs_set_sysfs_str(TSN_PATH, source);
        if (ret)
            MLOGI("hw multi demux Error ret 0x%x\n", ret);
    }
#endif
#endif
    return ret;
}

}
