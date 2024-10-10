/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef _AML_WV_IPTV_CAS_H_
#define _AML_WV_IPTV_CAS_H_

#include "cas/AmlCasBase.h"
#include <Aml_MP/Common.h>

#include <mutex>

class AmCasIPTV;

namespace aml_mp {

class AmlWVIptvCas : public AmlCasBase
{
public:
    AmlWVIptvCas(Aml_MP_CASServiceType serviceType);
    ~AmlWVIptvCas();
    virtual int startDescrambling(const Aml_MP_IptvCASParams* params) override;
    virtual int stopDescrambling() override;
    virtual int setPrivateData(const uint8_t* data, size_t size) override;
    virtual int processEcm(bool isSection, int ecmPid, const uint8_t* data, size_t size) override;
    virtual int processEmm(bool isSection, int emmPid, const uint8_t* data, size_t size) override;

private:
    AmCasIPTV * pIptvCas = nullptr;
    uint8_t sessionId[8];
    int mInstanceId;
    char mName[64];

    int mFirstEcm;
    uint8_t mEcmTsPacket[188];

    int setDscSource();
    int dscDevOpen(const char *port_addr, int flags);
    int checkEcmProcess(uint8_t* pBuffer, uint32_t vEcmPid, uint32_t aEcmPid, size_t * nSize);


    AmlWVIptvCas(const AmlWVIptvCas&) = delete;
    AmlWVIptvCas& operator= (const AmlWVIptvCas&) = delete;
};
}

#endif
