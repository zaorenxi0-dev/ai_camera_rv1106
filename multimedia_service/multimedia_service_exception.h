#ifndef MULTIMEDIA_SERVICE_EXCEPTION_H
#define MULTIMEDIA_SERVICE_EXCEPTION_H
#include "kn_service_base_exception.h"
#include "kn_util_sys.h"

class SvcException : public KNServiceBaseException
{
  public:
    SvcException();
    virtual ~SvcException();
    int dealResult() KN_OVERRIDE;
  private:
};

#endif // MULTIMEDIA_SERVICE_EXCEPTION_H
