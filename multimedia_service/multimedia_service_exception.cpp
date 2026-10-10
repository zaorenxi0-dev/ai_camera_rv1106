#include "multimedia_service_exception.h"
#include "logger.h"
SvcException::SvcException()
{
}

SvcException::~SvcException() {}

int SvcException::dealResult()
{
    bool ret = KNServiceBaseException::dealResult();
    if (ret)
    {
    }
    return ret;
}
