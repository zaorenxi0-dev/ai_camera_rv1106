#ifndef MULTIMEDIA_SERVICE_FACTORY_H
#define MULTIMEDIA_SERVICE_FACTORY_H

#include "service_base_factory.h"
#include "MultimediaServiceInterface.h"

class MultimediaServiceFactory
{
public:
    MultimediaServiceFactory();
    ~MultimediaServiceFactory();

     FactoryCreator<MultimediaServiceInterface> &getInstance(){
        return factoryCreator;
     }

private:
   FactoryCreator<MultimediaServiceInterface> factoryCreator;


};





#endif