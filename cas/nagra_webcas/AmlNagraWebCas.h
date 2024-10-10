/*
 * Copyright (c) 2020 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#ifndef _AML_Nagra_WEB_CAS_H_
#define _AML_Nagra_WEB_CAS_H_

#include <cas/AmlCasBase.h>
#include <Aml_MP/Common.h>
#include "cas/AmCasLibWrapper.h"

#include <mutex>

namespace aml_mp {

class AmlNagraWebCas : public AmlCasBase
{
public:
    AmlNagraWebCas(Aml_MP_CASServiceType serviceType);
    ~AmlNagraWebCas();
    virtual int startDescrambling(const Aml_MP_IptvCASParams* params) override;
    virtual int stopDescrambling() override;
    virtual int setPrivateData(const uint8_t* data, size_t size) override;
    virtual int processEcm(bool isSection, int ecmPid, const uint8_t* data, size_t size) override;
    virtual int decrypt(uint8_t *in, int size, void *ext_data, Aml_MP_Buffer* outbuffer) override;

private:
    char mServerPort[10];
    sptr<AmCasLibWrapper<AML_MP_CAS_SERVICE_NAGRA_WEB>> pIptvCas;
    uint8_t sessionId[8];

    AmlNagraWebCas(const AmlNagraWebCas&) = delete;
    AmlNagraWebCas& operator= (const AmlNagraWebCas&) = delete;
};
}

#endif
