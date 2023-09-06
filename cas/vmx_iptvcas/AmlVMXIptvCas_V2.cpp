/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#define LOG_TAG "AmlVMXIptvCas_V2"
#include <utils/AmlMpLog.h>
#include <utils/AmlMpUtils.h>
#include "AmlVMXIptvCas_V2.h"
#include <dlfcn.h>
#include <cutils/properties.h>
#include <utils/Amlsysfsutils.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

static const char* mName = LOG_TAG;


namespace aml_mp {

AmlVMXIptvCas_V2::AmlVMXIptvCas_V2(Aml_MP_CASServiceType serviceType)
:AmlCasBase(serviceType)
{
    MLOGI("ctor AmlVMXIptvCas_V2");
    memset(mEcmTsPacket, 0, sizeof(mEcmTsPacket));

    pIptvCas = new AmCasLibWrapper<AML_MP_CAS_SERVICE_VERIMATRIX_IPTV>("libdec_ca_vmx_iptv.so");
}

AmlVMXIptvCas_V2::~AmlVMXIptvCas_V2()
{
    MLOGI("dtor AmlVMXIptvCas_V2");
    int ret = 0;

    pIptvCas.clear();
}

int AmlVMXIptvCas_V2::startDescrambling(const Aml_MP_IptvCASParams* params)
{
    MLOG();

    AmlCasBase::startDescrambling(params);

    int ret = 0;
    iptvserverinfo_t initParam;
    memset(&initParam, 0, sizeof(initParam));

    initParam.enablelog = 1;
    initParam.serveraddr = (char*)params->serverAddress;
    snprintf(mServerPort, sizeof(mServerPort), "%d", params->serverPort);
    initParam.serverport = mServerPort;
    initParam.storepath = (char*)params->keyPath;
    pIptvCas->setCasInstanceId(params->demuxId);
    MLOGI("setCasInstanceId: demuxId: %d", params->demuxId);
    pIptvCas->setPrivateData((void *)&initParam, sizeof(iptvserverinfo_t));

    ret = pIptvCas->provision();

    if (pIptvCas) {
        setDscSource(TSN_IPTV);
        MLOGI("%s, vpid=0x%x, apid=0x%x, vecm:%#x, acem:%#x", __func__, params->videoPid, params->audioPid,
            params->ecmPid[1], params->ecmPid[0]);
        pIptvCas->setPids(params->videoPid, params->audioPid);
        ret = pIptvCas->openSession(&sessionId[0]);
    }

    return ret;
}

int AmlVMXIptvCas_V2::stopDescrambling()
{
    MLOG();
    int ret = 0;

    setDscSource(TSN_DVB);

    if (pIptvCas) {
        ret = pIptvCas->closeSession(&sessionId[0]);
        ret = pIptvCas->releaseAmCas();
        pIptvCas.clear();
    }

    return ret;
}

int AmlVMXIptvCas_V2::setPrivateData(const uint8_t* data, size_t size)
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

int AmlVMXIptvCas_V2::checkEcmProcess(uint8_t* pBuffer, uint32_t vEcmPid, uint32_t aEcmPid,size_t * nSize)
{
  int ret = 0;
  uint32_t pid = 0;
  unsigned int rem = *nSize;

  uint8_t * psync = pBuffer;
  uint8_t * current = NULL;

  while (rem >= TS_PACKET_SIZE)
  {
      if (*psync != 0x47)
      {
          ++psync;
          --rem;
          continue;
      }
      if ((*(psync) == 0x47) && ((rem == TS_PACKET_SIZE) || (*(psync+TS_PACKET_SIZE) == 0x47)))
      {
          current = psync;
          pid = (( current[1] << 8 | current[2]) & 0x1FFF);
          if (pid == vEcmPid || pid == aEcmPid)
          {
              if (memcmp(mEcmTsPacket + 4,psync + 4,TS_PACKET_SIZE- 4))
              {
                  memcpy(mEcmTsPacket, psync, TS_PACKET_SIZE);
                  std::string ecmDataStr;
                  char hex[3];
                  for (int i = 0; i < 64; i++) {
                      snprintf(hex, sizeof(hex), "%02X", mEcmTsPacket[i]);
                      ecmDataStr.append(hex);
                      ecmDataStr.append(" ");
                    }
                  MLOGI("checkEcmProcess, ecmDataStr.c_str()=%s", ecmDataStr.c_str());
                  if (pIptvCas)
                  {
                      if (pid == (uint32_t)mIptvCasParam.ecmPid[1])
                         ret = pIptvCas->processEcm(0, 1, mIptvCasParam.ecmPid[1], mIptvCasParam.ecmPid[0], mEcmTsPacket, TS_PACKET_SIZE);
                      else
                         ret = pIptvCas->processEcm(0, 0, mIptvCasParam.ecmPid[1], mIptvCasParam.ecmPid[0], mEcmTsPacket, TS_PACKET_SIZE);
                  }
              }
              if (mFirstEcm != 1) {
                  MLOGI("first_SetECM find\n");
                  mFirstEcm = 1;
              }
          }
      }
      psync += TS_PACKET_SIZE;
      rem -= TS_PACKET_SIZE;
  }

  return ret;
}


int AmlVMXIptvCas_V2::processEcm(bool isSection, int ecmPid, const uint8_t* data, size_t size)
{
    int ret = 0;

    //==============FIXME=============
    if (pIptvCas) {
        uint8_t *pdata = const_cast<uint8_t *>(data);
        MLOGV("%s, pid=0x%x, size=%zu", __func__, mIptvCasParam.videoPid, size);
        if (isSection) {
            if (ecmPid == mIptvCasParam.ecmPid[1])
                ret = pIptvCas->processEcm(1, 1, mIptvCasParam.ecmPid[1], mIptvCasParam.ecmPid[0], pdata, size);
            else
                ret = pIptvCas->processEcm(1, 0, mIptvCasParam.ecmPid[1], mIptvCasParam.ecmPid[0], pdata, size);
        } else {
           checkEcmProcess(pdata, mIptvCasParam.ecmPid[1], mIptvCasParam.ecmPid[0], &size);
        }
    }

    return ret;
}

int AmlVMXIptvCas_V2::processEmm(const uint8_t* data, size_t size)
{
    int ret = 0;

    if (pIptvCas) {
        uint8_t *pdata = const_cast<uint8_t *>(data);
        ret = pIptvCas->processEmm(0, mIptvCasParam.videoPid ,pdata, size);
    }

    return ret;
}

int AmlVMXIptvCas_V2::switchAudioTrack(const Aml_MP_AudioParams* params)
{
    MLOG();
    int ret = 0;
    const int tracktype_audio = 2;//tracktype is audio

    if (pIptvCas) {
        MLOGI("switchAudioTrack: trackType = %d, AudioPid = %d, AudioFormat = %d.", tracktype_audio, params->pid, params->audioCodec);
        ret = pIptvCas->selectTrack(tracktype_audio, params->pid, -1);   //-1 : audio format is not use.
    }

    return ret;

}

int AmlVMXIptvCas_V2::dscDevOpen(const char *port_addr, int flags)
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


int AmlVMXIptvCas_V2::setDscSource(const char* source)
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
