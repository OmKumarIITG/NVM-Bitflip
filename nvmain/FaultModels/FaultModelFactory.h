#ifndef __FAULTMODELFACTORY_H__
#define __FAULTMODELFACTORY_H__

#include <string>
#include "src/FaultModel.h"

namespace NVM {

class FaultModelFactory
{
  public:
    static FaultModel *CreateFaultModel(std::string model);
};

};

#endif 