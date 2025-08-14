#include "FaultModels/FaultModelFactory.h"
#include "FaultModels/ECP/ECP.h"
#include "FaultModels/NeuroHammer/NeuroHammer.h"

using namespace NVM;

FaultModel *FaultModelFactory::CreateFaultModel(std::string model)
{
    FaultModel *faultModel = NULL;

    if (model == "ECP")
    {
        faultModel = NULL; // currently not implemented
    }
    else if (model == "NeuroHammer")
    {
        faultModel = new NeuroHammer();
    }

    return faultModel;
} 