#include "neuralnet.h"
#include "util.h"
#include "version.h"
#include "sync.h"
#include "appcache.h"

#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <functional>
#include <future>
#include <fstream>
#include <map>
#include <array>
#include <cstdio>
#include <string>

// Old VB based NeuralNet.
extern std::string qtGetNeuralHash(std::string data);
extern std::string qtGetNeuralContract(std::string data);
extern double qtExecuteGenericFunction(std::string function,std::string data);
extern std::string qtExecuteDotNetStringFunction(std::string function,std::string data);
extern void qtSyncWithDPORNodes(std::string data);
int64_t IsNeural();
extern bool NeedASuperblock();

// While transitioning to dotnet the NeuralNet implementation has been split
// into 3 implementations; Win32 with Qt, Win32 without Qt and the rest.
// After the transition both Win32 implementations can be removed.
namespace NN
{
    // Win32 with Qt enabled.
    bool IsEnabled()
    {
        return GetArgument("disableneuralnetwork", "false") == "false";
    }

    std::string GetNeuralVersion()
    {
        int neural_id = static_cast<int>(IsNeural());
        return std::to_string(CLIENT_VERSION_MINOR) + "." + std::to_string(neural_id);
    }

    std::string GetNeuralHash(bool bOverride)
    {
        SetTestnetFlag(fTestNet);

        if (bOverride)
            return qtGetNeuralHash("");

        else
            return ContractAgeWithinBounds() ? qtGetNeuralHash("") : "";
    }

    std::string GetNeuralContract(bool bOverride)
    {
        SetTestnetFlag(fTestNet);

        if (bOverride)
            return qtGetNeuralContract("");

        else
            return ContractAgeWithinBounds() ? qtGetNeuralContract("") : "";
    }

    bool SetTestnetFlag(bool onTestnet)
    {
        return qtExecuteGenericFunction("SetTestNetFlag", onTestnet ? "TESTNET" : "MAINNET") != 0;
    }

    bool SynchronizeDPOR(const std::string& data)
    {
        qtSyncWithDPORNodes(data);
        return true;
    }

    std::string ExecuteDotNetStringFunction(std::string function, std::string data)
    {
        return qtExecuteDotNetStringFunction(function, data);
    }

    int64_t IsNeuralNet()
    {
       return IsNeural();
    }

    bool ContractAgeWithinBounds()
    {
        if (!NeedASuperblock())
            return true;

        int64_t nSBTime = ReadCache("superblock", "magnitudes").timestamp;
        int64_t nSBAge = GetAdjustedTime() - nSBTime;
        int64_t nNNContractAge = qtGetContractAge();
        int64_t nNNAge  = GetAdjustedTime() - (nNNContractAge + GetTimeOffset());
        int64_t nSBAgeDiff = nSBAge - 86400;

        if (fDebug10)
            LogPrintf("ContractAgeWithinBounds(): NN contract age = %" PRId64, nNNAge);

        if (nNNContractAge >= 1)
            return (nSBAgeDiff > nNNAge) ? true : false;

        else if (nNNContractAge == 0)
            if (fDebug10)
                LogPrintf("ContractAgeWithinBounds(): GlobalCom is not Initialized");

        else if (nNNContractAge == -1)
            if (fDebug10)
                LogPrintf("ContractAgeWithinBounds(): NN has no data");

        else if (nNNContractAge == -2)
            if (fDebug10)
                LogPrintf("ContractAgeWithinBounds(): NN is currently syncing");

        else if (nNNContractAge == -3)
            if (fDebug10)
                LogPrintf("ContractAgeWithinBounds(): NN reported an exception occured; Check debug2.log for details");

        return false;
    }
}
