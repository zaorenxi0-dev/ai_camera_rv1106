#ifndef MULTIMEDIA_SERVICE_INTERFACE_H
#define MULTIMEDIA_SERVICE_INTERFACE_H

#include "service_base.h"
#include "multimedia_service_define.h"
#include <vector>
class MultimediaServiceInterface : public ServiceBaseInterface
{
public:
    MultimediaServiceInterface();
    virtual ~MultimediaServiceInterface();
    
    virtual std::vector<camsvc::MultimediaVideoChannel> getVideoChannels() = 0;

    virtual int videoStatus(int index) = 0;

    virtual int audioStart() = 0;
    virtual int audioStop()  = 0;
};

#endif
