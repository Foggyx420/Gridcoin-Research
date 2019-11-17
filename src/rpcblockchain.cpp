// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "rpcserver.h"
#include "rpcprotocol.h"
#include "kernel.h"
#include "init.h" // for pwalletMain
#include "block.h"
#include "txdb.h"
#include "beacon.h"
#include "neuralnet/neuralnet.h"
#include "neuralnet/project.h"
#include "neuralnet/researcher.h"
#include "neuralnet/tally.h"
#include "backup.h"
#include "appcache.h"
#include "contract/polls.h"
#include "contract/contract.h"
#include "util.h"

#include <iostream>
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <algorithm>

#include <univalue.h>

extern CCriticalSection cs_ConvergedScraperStatsCache;
extern ConvergedScraperStats ConvergedScraperStatsCache;

using namespace std;

extern std::string YesNo(bool bin);
extern double DoubleFromAmount(int64_t amount);
std::string PubKeyToAddress(const CScript& scriptPubKey);
const CBlockIndex* GetHistoricalMagnitude(const NN::MiningId mining_id);
extern std::string GetProvableVotingWeightXML();
bool AskForOutstandingBlocks(uint256 hashStart);
bool ForceReorganizeToHash(uint256 NewHash);
extern UniValue GetUpgradedBeaconReport();
extern UniValue MagnitudeReport(const NN::Cpid cpid);
bool StrLessThanReferenceHash(std::string rh);
extern std::string ExtractValue(std::string data, std::string delimiter, int pos);
extern UniValue SuperblockReport(int lookback = 14, bool displaycontract = false, std::string cpid = "");
bool LoadAdminMessages(bool bFullTableScan,std::string& out_errors);
std::string ExtractXML(const std::string& XMLdata, const std::string& key, const std::string& key_end);
extern bool AdvertiseBeacon(std::string &sOutPrivKey, std::string &sOutPubKey, std::string &sError, std::string &sMessage);
extern bool ScraperSynchronizeDPOR();

double Round(double d, int place);

std::string GetNeuralNetworkSupermajorityHash(double& out_popularity);
std::string GetCurrentNeuralNetworkSupermajorityHash(double& out_popularity);

UniValue GetJSONNeuralNetworkReport();
UniValue GetJSONCurrentNeuralNetworkReport();

extern UniValue GetJSONVersionReport();
extern UniValue GetJsonUnspentReport();

bool GetEarliestStakeTime(std::string grcaddress, std::string cpid);
extern UniValue GetJSONBeaconReport();

double GetTotalBalance();

double CoinToDouble(double surrogate);
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);

BlockFinder RPCBlockFinder;

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }

    int nShift = (blockindex->nBits >> 24) & 0xff;

    double dDiff =
            (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}




double GetBlockDifficulty(unsigned int nBits)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    int nShift = (nBits >> 24) & 0xff;

    double dDiff =
            (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

namespace {
UniValue ClaimToJson(const NN::Claim& claim)
{
    UniValue json(UniValue::VOBJ);

    json.pushKV("version", (int)claim.m_version);
    json.pushKV("mining_id", claim.m_mining_id.ToString());
    json.pushKV("client_version", claim.m_client_version);
    json.pushKV("organization", claim.m_organization);

    json.pushKV("block_subsidy", claim.m_block_subsidy);

    json.pushKV("research_subsidy", claim.m_research_subsidy);
    json.pushKV("magnitude", claim.m_magnitude);
    json.pushKV("magnitude_unit", claim.m_magnitude_unit);

    json.pushKV("signature", EncodeBase64(claim.m_signature.data(), claim.m_signature.size()));

    json.pushKV("quorum_hash", claim.m_quorum_hash.ToString());
    json.pushKV("quorum_address", claim.m_quorum_address);

    return json;
}

UniValue SuperblockToJson(const NN::Superblock& superblock)
{
    UniValue magnitudes(UniValue::VOBJ);

    for (const auto& cpid_pair : superblock.m_cpids) {
        magnitudes.pushKV(cpid_pair.first.ToString(), cpid_pair.second);
    }

    UniValue projects(UniValue::VOBJ);

    for (const auto& project_pair : superblock.m_projects) {
        UniValue project(UniValue::VOBJ);

        project.pushKV("average_rac", project_pair.second.m_average_rac);
        project.pushKV("rac", project_pair.second.m_rac);
        project.pushKV("total_credit", project_pair.second.m_total_credit);

        projects.pushKV(project_pair.first, project);
    }

    UniValue json(UniValue::VOBJ);

    json.pushKV("version", (int)superblock.m_version);
    json.pushKV("magnitudes", std::move(magnitudes));
    json.pushKV("projects", std::move(projects));

    return json;
}
} // anonymous namespace

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool fPrintTransactionDetail)
{
    UniValue result(UniValue::VOBJ);

    result.pushKV("hash", block.GetHash().GetHex());

    CMerkleTx txGen(block.vtx[0]);
    txGen.SetMerkleBranch(&block);

    result.pushKV("confirmations", txGen.GetDepthInMainChain());
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    result.pushKV("mint", CoinToDouble(blockindex->nMint));
    result.pushKV("MoneySupply", blockindex->nMoneySupply);
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("nonce", (uint64_t)block.nNonce);
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("blocktrust", leftTrim(blockindex->GetBlockTrust().GetHex(), '0'));
    result.pushKV("chaintrust", leftTrim(blockindex->nChainTrust.GetHex(), '0'));

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    if (blockindex->pnext)
        result.pushKV("nextblockhash", blockindex->pnext->GetBlockHash().GetHex());

    const NN::Claim& claim = block.GetClaim();

    std::string PoRNarr = "";
    if (blockindex->IsProofOfStake() && claim.HasResearchReward()) {
        PoRNarr = "proof-of-research";
    }

    result.pushKV("flags", strprintf("%s%s",
        blockindex->IsProofOfStake() ? "proof-of-stake" : "proof-of-work",
        blockindex->GeneratedStakeModifier()? " stake-modifier": "") + " " + PoRNarr);

    result.pushKV("proofhash", blockindex->hashProof.GetHex());
    result.pushKV("entropybit", (int)blockindex->GetStakeEntropyBit());
    result.pushKV("modifier", strprintf("%016" PRIx64, blockindex->nStakeModifier));
    result.pushKV("modifierchecksum", strprintf("%08x", blockindex->nStakeModifierChecksum));

    UniValue txinfo(UniValue::VARR);

    for (auto const& tx : block.vtx)
    {
        if (fPrintTransactionDetail)
        {
            UniValue entry(UniValue::VOBJ);

            entry.pushKV("txid", tx.GetHash().GetHex());
            TxToJSON(tx, uint256(), entry);

            txinfo.push_back(entry);
        }
        else
            txinfo.push_back(tx.GetHash().GetHex());
    }

    result.pushKV("tx", txinfo);

    if (block.IsProofOfStake())
        result.pushKV("signature", HexStr(block.vchBlockSig.begin(), block.vchBlockSig.end()));

    result.pushKV("claim", ClaimToJson(block.GetClaim()));

    if (fDebug3) result.pushKV("BoincHash",block.vtx[0].hashBoinc);

    if (fPrintTransactionDetail && blockindex->nIsSuperBlock == 1) {
        result.pushKV("superblock", SuperblockToJson(block.GetSuperblock()));
    }

    result.pushKV("IsSuperBlock", (int)blockindex->nIsSuperBlock);
    result.pushKV("IsContract", (int)blockindex->nIsContract);

    return result;
}


UniValue showblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "showblock <index>\n"
                "\n"
                "<index> Block number\n"
                "\n"
                "Returns all information about the block at <index>\n");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range\n");

    LOCK(cs_main);

    CBlockIndex* pblockindex = RPCBlockFinder.FindByHeight(nHeight);

    if (pblockindex==NULL)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    CBlock block;
    block.ReadFromDisk(pblockindex);
    return blockToJSON(block, pblockindex, false);
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getbestblockhash\n"
                "\n"
                "Returns the hash of the best block in the longest block chain\n");

    LOCK(cs_main);

    return hashBestChain.GetHex();
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getblockcount\n"
                "\n"
                "Returns the number of blocks in the longest block chain\n");

    LOCK(cs_main);

    return nBestHeight;
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getdifficulty\n"
                "\n"
                "Returns the difficulty as a multiple of the minimum difficulty\n");

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("proof-of-work",        GetDifficulty());
    obj.pushKV("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true)));
    return obj;
}

UniValue settxfee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1 || AmountFromValue(params[0]) < MIN_TX_FEE)
        throw runtime_error(
                "settxfee <amount>\n"
                "\n"
                "<amount> is a real and is rounded to the nearest 0.01\n"
                "\n"
                "Sets the txfee for transactions\n");

    LOCK(cs_main);

    nTransactionFee = AmountFromValue(params[0]);
    nTransactionFee = (nTransactionFee / CENT) * CENT;  // round to cent

    return true;
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getrawmempool\n"
                "\n"
                "Returns all transaction ids in memory pool\n");


    vector<uint256> vtxid;

    {
        LOCK(mempool.cs);

        mempool.queryHashes(vtxid);
    }

    UniValue a(UniValue::VARR);
    for (auto const& hash : vtxid)
        a.push_back(hash.ToString());

    return a;
}

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getblockhash <index>\n"
                "\n"
                "<index> Block number for requested hash\n"
                "\n"
                "Returns hash of block in best-block-chain at <index>\n");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range.");
    if (fDebug10)
        LogPrintf("Getblockhash %d", nHeight);

    LOCK(cs_main);

    CBlockIndex* RPCpblockindex = RPCBlockFinder.FindByHeight(nHeight);

    return RPCpblockindex->phashBlock->GetHex();
}

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "getblock <hash> [bool:txinfo]\n"
                "\n"
                "[bool:txinfo] optional to print more detailed tx info\n"
                "\n"
                "Returns details of a block with given block-hash\n");

    std::string strHash = params[0].get_str();
    uint256 hash = uint256S(strHash);

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    LOCK(cs_main);

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

UniValue getblockbynumber(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "getblockbynumber <number> [bool:txinfo]\n"
                "\n"
                "[bool:txinfo] optional to print more detailed tx info\n"
                "\n"
                "Returns details of a block with given block-number\n");

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > nBestHeight)
        throw runtime_error("Block number out of range");

    LOCK(cs_main);

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return blockToJSON(block, pblockindex, params.size() > 1 ? params[1].get_bool() : false);
}

std::string ExtractValue(std::string data, std::string delimiter, int pos)
{
    std::vector<std::string> vKeys = split(data.c_str(),delimiter);
    std::string keyvalue = "";
    if (vKeys.size() > (unsigned int)pos)
    {
        keyvalue = vKeys[pos];
    }

    return keyvalue;
}

bool AdvertiseBeacon(std::string &sOutPrivKey, std::string &sOutPubKey, std::string &sError, std::string &sMessage)
{
    sOutPrivKey = "BUG! deprecated field used";
    LOCK(cs_main);
    {
        const std::string primary_cpid = NN::GetPrimaryCpid();

        if (!IsResearcher(primary_cpid))
        {
            sError = "INVESTORS_CANNOT_SEND_BEACONS";
            return false;
        }

        //If beacon is already in the chain, exit early
        if (!GetBeaconPublicKey(primary_cpid, true).empty())
        {
            // Ensure they can re-send the beacon if > 5 months old : GetBeaconPublicKey returns an empty string when > 5 months: OK.
            // Note that we allow the client to re-advertise the beacon in 5 months, so that they have a seamless and uninterrupted keypair in use (prevents a hacker from hijacking a keypair that is in use)
            sError = "ALREADY_IN_CHAIN";
            return false;
        }

        // Prevent users from advertising multiple times in succession by setting a limit of one advertisement per 5 blocks.
        // Realistically 1 should be enough however just to be sure we deny advertisements for 5 blocks.
        static int nLastBeaconAdvertised = 0;
        if ((nBestHeight - nLastBeaconAdvertised) < 5)
        {
            sError = _("A beacon was advertised less then 5 blocks ago. Please wait a full 5 blocks for your beacon to enter the chain.");
            return false;
        }

        uint256 hashRand = GetRandHash();
        double nBalance = GetTotalBalance();
        if (nBalance < 1.01)
        {
            sError = "Balance too low to send beacon, 1.01 GRC minimum balance required.";
            return false;
        }

        CKey keyBeacon;
        if(!GenerateBeaconKeys(primary_cpid, keyBeacon))
        {
            sError = "GEN_KEY_FAIL";
            return false;
        }

        // Convert the new pubkey into legacy hex format
        sOutPubKey = HexStr(keyBeacon.GetPubKey().Raw());

        std::string GRCAddress = DefaultWalletAddress();
        // Public Signing Key is stored in Beacon
        std::string contract = "UNUSED;" + hashRand.GetHex() + ";" + GRCAddress + ";" + sOutPubKey;
        LogPrintf("Creating beacon for cpid %s, %s",primary_cpid, contract);
        std::string sBase = EncodeBase64(contract);
        std::string sAction = "add";
        std::string sType = "beacon";
        std::string sName = primary_cpid;
        try
        {
            // Backup config with old keys like a normal backup
            // not needed, but extra backup does not hurt.
            // Also back up the wallet for extra safety measure.

            LOCK(pwalletMain->cs_wallet);

            if(!BackupConfigFile(GetBackupFilename("gridcoinresearch.conf"))
                    || !BackupWallet(*pwalletMain, GetBackupFilename("wallet.dat")))
            {
                sError = "Failed to backup old configuration file and wallet. Beacon not sent.";
                return false;
            }

            // Send the beacon transaction
            sMessage = SendContract(sType,sName,sBase);

            // This prevents repeated beacons
            nLastBeaconAdvertised = nBestHeight;

            return true;
        }
        catch(UniValue& objError)
        {
            sError = "Error: Unable to send beacon::"+objError.write();
            return false;
        }
        catch (std::exception &e)
        {
            sError = "Error: Unable to send beacon;:"+std::string(e.what());
            return false;
        }
    }
}

// Rpc

UniValue backupprivatekeys(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "backupprivatekeys\n"
                "\n"
                "Backup wallet private keys to file (Wallet must be fully unlocked!)\n");

    string sErrors;
    string sTarget;
    UniValue res(UniValue::VOBJ);

    bool bBackupPrivateKeys = BackupPrivateKeys(*pwalletMain, sTarget, sErrors);

    if (!bBackupPrivateKeys)
        res.pushKV("error", sErrors);

    else
        res.pushKV("location", sTarget);

    res.pushKV("result", bBackupPrivateKeys);

    return res;
}

UniValue rain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "rain [UniValue](UniValue::VARR)\n"
                "\n"
                "[UniValue] -> Address<COL>Amount<ROW>...(UniValue::VARR)\n"
                "\n"
                "rains coins on the network\n");

    UniValue res(UniValue::VOBJ);
    std::string sNarr = executeRain(params[0].get_str());
    res.pushKV("Response", sNarr);
    return res;
}

UniValue rainbymagnitude(const UniValue& params, bool fHelp)
    {
    if (fHelp || (params.size() < 2 || params.size() > 3))
        throw runtime_error(
                "rainbymagnitude <whitelisted project> <amount> [message]\n"
                "\n"
                "<whitelisted project> --> Required: If a project is specified, rain will be limited to that project. Use * for network-wide.\n"
                "<amount> --> Required: Specify amount of coints in double to be rained\n"
                "[message] -> Optional: Provide a message rained to all rainees\n"
                "\n"
                "rain coins by magnitude on network");

    UniValue res(UniValue::VOBJ);

    std::string sProject = params[0].get_str();

    if (fDebug) LogPrintf("rainbymagnitude: sProject = %s", sProject.c_str());

    double dAmount = params[1].get_real();

    if (dAmount <= 0)
        throw runtime_error("Amount must be greater then 0");

    std::string sMessage = "";

    if (params.size() > 2)
        sMessage = params[2].get_str();

    // Make sure statistics are up to date. This will do nothing if a convergence has already been cached and is clean.
    bool bStatsAvail = ScraperSynchronizeDPOR();

    if (!bStatsAvail)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "Wallet has not formed a convergence from statistics information.");
    }

    ScraperStats mScraperConvergedStats;
    {
        LOCK(cs_ConvergedScraperStatsCache);

        // Make a local copy of the cached stats in the convergence and release the lock.
        mScraperConvergedStats = ConvergedScraperStatsCache.mScraperConvergedStats;
    }

    if (fDebug) LogPrintf("rainbymagnitude: mScraperConvergedStats size = %u", mScraperConvergedStats.size());

    double dTotalAmount = 0;
    int64_t nTotalAmount = 0;

    double dTotalMagnitude = 0;

    statsobjecttype rainbymagmode = (sProject == "*" ? statsobjecttype::byCPID : statsobjecttype::byCPIDbyProject);

    //------- CPID ----------------CPID address -- Mag
    std::map<NN::Cpid, std::pair<CBitcoinAddress, double>> mCPIDRain;

    for (const auto& entry : mScraperConvergedStats)
    {
        // Only consider entries along the specfied dimension
        if (entry.first.objecttype == rainbymagmode)
        {
            NN::Cpid CPIDKey;

            if (rainbymagmode == statsobjecttype::byCPIDbyProject)
            {
                std::vector<std::string> vObjectStatsKey = split(entry.first.objectID, ",");

                // Only process elements that match the specified project if in project level rain.
                if (vObjectStatsKey[0] != sProject) continue;

                CPIDKey = NN::Cpid::Parse(vObjectStatsKey[1]);
            }
            else
            {
                CPIDKey = NN::Cpid::Parse(entry.first.objectID);
            }

            double dCPIDMag = std::round(entry.second.statsvalue.dMag);

            // Zero mag CPIDs do not get paid.
            if (!dCPIDMag) continue;

            // Find beacon grc address
            std::string scacheContract = ReadCache(Section::BEACON, CPIDKey.ToString()).value;

            // Should never occur but we know seg faults can occur in some cases
            if (scacheContract.empty()) continue;

            std::string sContract = DecodeBase64(scacheContract);
            std::string sGRCAddress = ExtractValue(sContract, ";", 2);

            if (fDebug) LogPrintf("INFO: rainbymagnitude: sGRCaddress = %s.", sGRCAddress);

            CBitcoinAddress address(sGRCAddress);

            if (!address.IsValid())
            {
                LogPrintf("ERROR: rainbymagnitude: Invalid Gridcoin address: %s.", sGRCAddress);
                continue;
            }

            mCPIDRain[CPIDKey] = std::make_pair(address, dCPIDMag);

            // Increment the accumulated mag. This will be equal to the total mag of the valid CPIDs entered
            // into the RAIN map, and will be used to normalize the payments.
            dTotalMagnitude += dCPIDMag;

            if (fDebug) LogPrintf("rainmagnitude: CPID = %s, address = %s, dCPIDMag = %f",
                                  CPIDKey.ToString(), sGRCAddress, dCPIDMag);
        }
    }

    if (mCPIDRain.empty() || !dTotalMagnitude)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No CPIDs to pay and/or total CPID magnitude is zero. This could be caused by an incorrect project specified.");
    }

    std::vector<std::pair<CScript, int64_t> > vecSend;

    // Setup the payment vector now that the CPID entries and mags have been validated and the total mag is computed.
    for(const auto& iter : mCPIDRain)
    {
        double dCPIDMag = iter.second.second;

            double dPayout = (dCPIDMag / dTotalMagnitude) * dAmount;

            dTotalAmount += dPayout;

            CScript scriptPubKey;
            scriptPubKey.SetDestination(iter.second.first.Get());

            int64_t nAmount = roundint64(dPayout * COIN);
            nTotalAmount += nAmount;

            vecSend.push_back(std::make_pair(scriptPubKey, nAmount));

            if (fDebug) LogPrintf("rainmagnitude: address = %s, amount = %f", iter.second.first.ToString(), CoinToDouble(nAmount));
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CWalletTx wtx;
    wtx.mapValue["comment"] = "Rain By Magnitude";
    wtx.hashBoinc = "<NARR>Rain By Magnitude: " + MakeSafeMessage(sMessage) + "</NARR>";

    EnsureWalletIsUnlocked();
    // Check funds
    double dBalance = GetTotalBalance();

    if (dTotalAmount > dBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");
    // Send
    CReserveKey keyChange(pwalletMain);

    int64_t nFeeRequired = 0;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired);

    LogPrintf("Rain By Magnitude Transaction Created.");

    if (!fCreated)
    {
        if (nTotalAmount + nFeeRequired > pwalletMain->GetBalance())
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction creation failed");
    }

    LogPrintf("Committing.");
    // Rain the recipients
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
    {
        LogPrintf("Rain By Magnitude Commit failed.");

        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
}
    res.pushKV("Rain By Magnitude",  "Sent");
    res.pushKV("TXID", wtx.GetHash().GetHex());
    res.pushKV("Rain Amount Sent", dTotalAmount);
    res.pushKV("TX Fee", ((double)nFeeRequired) / COIN);
    res.pushKV("# of Recipients", (uint64_t)vecSend.size());

    if (!sMessage.empty())
        res.pushKV("Message", sMessage);

    return res;
}

UniValue unspentreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "unspentreport\n"
                "\n"
                "Displays unspentreport\n");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue aUnspentReport = GetJsonUnspentReport();

    return aUnspentReport;
}

UniValue advertisebeacon(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "advertisebeacon\n"
                "\n"
                "Advertise a beacon (Requires wallet to be fully unlocked)\n");

    EnsureWalletIsUnlocked();

    UniValue res(UniValue::VOBJ);

    /* Try to copy key from config. The call is no-op if already imported or
     * nothing to import. This saves a migrating users from copy-pasting
     * the key string to importprivkey command.
     */
    const std::string primary_cpid = NN::GetPrimaryCpid();
    bool importResult= ImportBeaconKeysFromConfig(primary_cpid, pwalletMain);
    res.pushKV("ConfigKeyImported", importResult);

    std::string sOutPubKey = "";
    std::string sOutPrivKey = "";
    std::string sError = "";
    std::string sMessage = "";
    bool fResult = AdvertiseBeacon(sOutPrivKey,sOutPubKey,sError,sMessage);

    res.pushKV("Result", fResult ? "SUCCESS" : "FAIL");
    res.pushKV("CPID",primary_cpid.c_str());
    res.pushKV("Message",sMessage.c_str());

    if (!sError.empty())
        res.pushKV("Errors",sError);

    if (!fResult)
    {
        res.pushKV("FAILURE","Note: if your wallet is locked this command will fail; to solve that unlock the wallet: 'walletpassphrase <yourpassword> <240>'.");
    }
    else
    {
        res.pushKV("Public Key",sOutPubKey.c_str());
    }

    return res;
}

UniValue beaconreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "beaconreport\n"
                "\n"
                "Displays list of valid beacons in the network\n");

    LOCK(cs_main);

    UniValue res = GetJSONBeaconReport();

    return res;
}

UniValue beaconstatus(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "beaconstatus [cpid]\n"
                "\n"
                "[cpid] -> Optional parameter of cpid\n"
                "\n"
                "Displays status of your beacon or specified beacon on the network\n");

    UniValue res(UniValue::VOBJ);

    // Search for beacon, and report on beacon status.

    std::string sCPID = NN::GetPrimaryCpid();

    if (params.size() > 0)
        sCPID = params[0].get_str();

    LOCK(cs_main);

    std::string sPubKey =  GetBeaconPublicKey(sCPID, false);
    int64_t iBeaconTimestamp = BeaconTimeStamp(sCPID);
    std::string timestamp = TimestampToHRDate(iBeaconTimestamp);
    bool hasBeacon = HasActiveBeacon(sCPID);

    res.pushKV("CPID", sCPID);
    res.pushKV("Beacon Exists",YesNo(hasBeacon));
    res.pushKV("Beacon Timestamp",timestamp.c_str());
    res.pushKV("Public Key", sPubKey.c_str());
    res.pushKV("Private Key", "not-shown"); //TODO: show the key?

    std::string sErr = "";

    if (sPubKey.empty())
        sErr += "Public Key Missing. ";

    // Prior superblock Magnitude
    double dMagnitude = NN::Tally::GetMagnitude(NN::MiningId::Parse(sCPID));

    res.pushKV("Magnitude (As of last superblock)", dMagnitude);

    const bool is_mine = sCPID == NN::GetPrimaryCpid();
    res.pushKV("Mine", is_mine);

    if (is_mine && dMagnitude == 0)
        res.pushKV("Warning","Your magnitude is 0 as of the last superblock: this may keep you from staking POR blocks.");

    if (!sPubKey.empty() && is_mine)
    {
        EnsureWalletIsUnlocked();

        bool bResult;
        std::string sSignature;
        std::string sError;

        // Staking Test 10-15-2016 - Simulate signing an actual block to verify this CPID keypair will work.
        uint256 hashBlock = GetRandHash();

        bResult = SignBlockWithCPID(sCPID, hashBlock.GetHex(), sSignature, sError);

        if (!bResult)
        {
            sErr += "Failed to sign block with cpid: ";
            sErr += sError;
            sErr += "; ";
        }

        bool fResult = VerifyCPIDSignature(sCPID, hashBlock.GetHex(), sSignature);

        res.pushKV("Block Signing Test Results", fResult);

        if (!fResult)
            sErr += "Failed to sign POR block.  This can happen if your keypair is invalid.  Check walletbackups for the correct keypair, or request that your beacon is deleted. ";
    }

    if (!sErr.empty())
    {
        res.pushKV("Errors", sErr);
        res.pushKV("Help", "Note: If your beacon is missing its public key, or is not in the chain, you may try: advertisebeacon.");
        res.pushKV("Configuration Status","FAIL");
    }

    else
        res.pushKV("Configuration Status", "SUCCESSFUL");

    return res;
}

UniValue currentneuralhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "currentneuralhash\n"
                "\n"
                "Displays information for the current popular neural hash in network\n");

    UniValue res(UniValue::VOBJ);

    double popularity = 0;

    LOCK(cs_main);

    std::string consensus_hash = GetCurrentNeuralNetworkSupermajorityHash(popularity);

    res.pushKV("Popular",consensus_hash);

    return res;
}

UniValue currentneuralreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "currentneuralreport\n"
                "\n"
                "Displays information for the current neural hashes in network\n");

    LOCK(cs_main);

    UniValue res = GetJSONCurrentNeuralNetworkReport();

    return res;
}

UniValue explainmagnitude(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
                "explainmagnitude\n"
                "\n"
                "Itemize your CPID magnitudes by project.\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    const std::string primary_cpid = NN::GetPrimaryCpid();
    std::string sNeuralResponse = NN::GetInstance()->ExplainMagnitude(primary_cpid);

    if (sNeuralResponse.length() < 25)
    {
        res.pushKV("Neural Response", "false; Try again at a later time");

    }
    else
    {
        res.pushKV("Neural Response", "true (from THIS node)");

        std::vector<std::string> vMag = split(sNeuralResponse.c_str(),"<ROW>");

        for (unsigned int i = 0; i < vMag.size(); i++)
            res.pushKV(RoundToString(i+1,0),vMag[i].c_str());
    }

    return res;
}

UniValue lifetime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "lifetime\n"
                "\n"
                "Displays information for the lifetime of your cpid in the network\n");

    UniValue results(UniValue::VARR);
    UniValue c(UniValue::VOBJ);
    UniValue res(UniValue::VOBJ);

    const NN::CpidOption cpid = NN::Researcher::Get()->Id().TryCpid();
    std::string Narr = ToString(GetAdjustedTime());

    c.pushKV("Lifetime Payments Report", Narr);
    results.push_back(c);

    if (!cpid) {
        return results;
    }

    LOCK(cs_main);

    CBlockIndex* pindex = pindexGenesisBlock;

    while (pindex->nHeight < pindexBest->nHeight)
    {
        pindex = pindex->pnext;

        if (pindex==NULL || !pindex->IsInMainChain())
            continue;

        if (pindex == pindexBest)
            break;

        if (pindex->GetMiningId() == *cpid && (pindex->nResearchSubsidy > 0))
            res.pushKV(ToString(pindex->nHeight), RoundToString(pindex->nResearchSubsidy, 2));
    }
    //8-14-2015
    const NN::ResearchAccount account = NN::Tally::GetAccount(*cpid);

    res.pushKV("RA Magnitude Sum", (int)account.m_total_magnitude);
    res.pushKV("RA Accuracy", (int)account.m_accuracy);
    results.push_back(res);

    return results;
}

UniValue magnitude(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "magnitude <cpid>\n"
                "\n"
                "<cpid> -> cpid to look up\n"
                "\n"
                "Displays information for the magnitude of all cpids or specified in the network\n");

    const NN::MiningId mining_id = params.size() > 0
        ? NN::MiningId::Parse(params[0].get_str())
        : NN::Researcher::Get()->Id();

    if (!mining_id.Valid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid CPID.");
    }

    if (const NN::CpidOption cpid = mining_id.TryCpid()) {
        LOCK(cs_main);

        return MagnitudeReport(*cpid);
    }

    throw JSONRPCError(RPC_INVALID_PARAMETER, "No data for investor.");
}

UniValue myneuralhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "myneuralhash\n"
                "\n"
                "Displays information about your neural networks client current hash\n");

    UniValue res(UniValue::VOBJ);

    std::string myNeuralHash = NN::GetInstance()->GetNeuralHash();

    res.pushKV("My Neural Hash", myNeuralHash.c_str());

    return res;
}

UniValue neuralhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "neuralhash\n"
                "\n"
                "Displays information about the popular\n");

    UniValue res(UniValue::VOBJ);

    double popularity = 0;
    std::string consensus_hash = GetNeuralNetworkSupermajorityHash(popularity);

    res.pushKV("Popular", consensus_hash);

    return res;
}

UniValue neuralreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "neuralreport\n"
                "\n"
                "Displays neural report for the network\n");

    LOCK(cs_main);

    UniValue res = GetJSONNeuralNetworkReport();

    return res;
}

UniValue resetcpids(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "resetcpids\n"
                "\n"
                "Reloads cpids\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    ReadConfigFile(mapArgs, mapMultiArgs);
    NN::Researcher::Reload();

    res.pushKV("Reset", 1);

    return res;
}

UniValue staketime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "staketime\n"
                "\n"
                "Display information about staking time\n");

    UniValue res(UniValue::VOBJ);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    const std::string cpid = NN::GetPrimaryCpid();
    const std::string GRCAddress = DefaultWalletAddress();
    GetEarliestStakeTime(GRCAddress, cpid);

    res.pushKV("GRCTime", ReadCache(Section::GLOBAL, "nGRCTime").timestamp);
    res.pushKV("CPIDTime", ReadCache(Section::GLOBAL, "nCPIDTime").timestamp);

    return res;
}

UniValue superblockage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "superblockage\n"
                "\n"
                "Display information regarding superblock age\n");

    UniValue res(UniValue::VOBJ);

    const NN::SuperblockPtr superblock = NN::Tally::CurrentSuperblock();

    res.pushKV("Superblock Age", superblock->Age());
    res.pushKV("Superblock Timestamp", TimestampToHRDate(superblock->m_timestamp));
    res.pushKV("Superblock Block Number", superblock->m_height);
    res.pushKV("Pending Superblock Height", ReadCache(Section::NEURALSECURITY, "pending").value);

    return res;
}

UniValue superblocks(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
                "superblocks [lookback [displaycontract [cpid]]]\n"
                "\n"
                "[lookback] -> Optional: # of SB's to show (default 14)\n"
                "[displaycontract] -> Optional true/false: display SB contract (default false)\n"
                "[cpid] -> Optional: Shows magnitude for a cpid for recent superblocks\n"
                "\n"
                "Display data on recent superblocks\n");

    UniValue res(UniValue::VARR);

    int lookback = 14;
    bool displaycontract = false;
    std::string cpid = "";

    if (params.size() > 0)
    {
        lookback = params[0].get_int();
        if (fDebug) LogPrintf("INFO: superblocks: lookback %i", lookback);
    }

    if (params.size() > 1)
    {
        displaycontract = params[1].get_bool();
        if (fDebug) LogPrintf("INFO: superblocks: display contract %i", displaycontract);
    }

    if (params.size() > 2)
    {
        cpid = params[2].get_str();
        if (fDebug) LogPrintf("INFO: superblocks: CPID %s", cpid);
    }

    LOCK(cs_main);

    res = SuperblockReport(lookback, displaycontract, cpid);

    return res;
}

UniValue upgradedbeaconreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "upgradedbeaconreport\n"
                "\n"
                "Display upgraded beacon report of the network\n");

    LOCK(cs_main);

    UniValue aUpgBR = GetUpgradedBeaconReport();

    return aUpgBR;
}

UniValue addkey(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw runtime_error(
                "addkey <action> <keytype> <keyname> <keyvalue>\n"
                "\n"
                "<action> ---> Specify add or delete of key\n"
                "<keytype> --> Specify keytype ex: project\n"
                "<keyname> --> Specify keyname ex: milky\n"
                "<keyvalue> -> Specify keyvalue ex: 1\n"
                "\n"
                "Add a key to the network\n");

    UniValue res(UniValue::VOBJ);

    //To whitelist a project:
    //execute addkey add project projectname 1
    //To blacklist a project:
    //execute addkey delete project projectname 1
    //Key examples:

    //execute addkey add project milky 1
    //execute addkey delete project milky 1
    //execute addkey add project milky 2
    //execute addkey add price grc .0000046
    //execute addkey add price grc .0000045
    //execute addkey delete price grc .0000045
    //GRC will only memorize the *last* value it finds for a key in the highest block
    //execute memorizekeys
    //execute listdata project
    //execute listdata price
    //execute addkey add project grid20

    std::string sAction = params[0].get_str();
    bool bAdd = (sAction == "add") ? true : false;
    std::string sType = params[1].get_str();
    std::string sName = params[2].get_str();
    std::string sValue = params[3].get_str();

    bool bProjectKey = (sType == "project" || sType == "projectmapping"
        || (sType == "beacon" && sAction == "delete")
        || sType == "protocol"
        || sType == "scraper"
    );

    const std::string sPass = bProjectKey
            ? GetArgument("masterprojectkey", msMasterMessagePrivateKey)
            : msMasterMessagePrivateKey;

    res.pushKV("Action", sAction);
    res.pushKV("Type", sType);
    res.pushKV("Passphrase", sPass);
    res.pushKV("Name", sName);
    res.pushKV("Value", sValue);

    std::string result = SendMessage(bAdd, sType, sName, sValue, sPass, AmountFromValue(5), .1, "");

    res.pushKV("Results", result);

    return res;
}

UniValue currentcontractaverage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "currentcontractaverage\n"
                "\n"
                "Displays information on your current contract average with regards to superblock contract\n");

    UniValue res(UniValue::VOBJ);

    const NN::Superblock superblock = NN::GetInstance()->GetSuperblockContract();

    res.pushKV("Contract", SuperblockToJson(superblock));
    res.pushKV("beacon_count", (uint64_t)superblock.m_cpids.TotalCount());
    res.pushKV("avg_mag", superblock.m_cpids.AverageMagnitude());
    res.pushKV("beacon_participant_count", (uint64_t)superblock.m_cpids.size());
    res.pushKV("superblock_valid", superblock.WellFormed());
    res.pushKV(".NET Neural Hash", superblock.GetHash().ToString());
    res.pushKV("Length", (uint64_t)GetSerializeSize(superblock, SER_NETWORK, PROTOCOL_VERSION));
    res.pushKV("Wallet Neural Hash", superblock.GetHash().ToString());

    return res;
}

UniValue debug(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debug <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebug = params[0].get_bool();

    res.pushKV("Debug", fDebug ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}

UniValue debug10(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debug10 <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebug10 = params[0].get_bool();

    res.pushKV("Debug10", fDebug10 ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}

UniValue debug2(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debug2 <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebug2 = params[0].get_bool();

    res.pushKV("Debug2", fDebug2 ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}

UniValue debug3(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debug3 <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebug3 = params[0].get_bool();

    res.pushKV("Debug3", fDebug3 ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}

UniValue debug4(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debug4 <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebug4 = params[0].get_bool();

    res.pushKV("Debug4", fDebug4 ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}
UniValue debugnet(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "debugnet <bool>\n"
                "\n"
                "<bool> -> Specify true or false\n"
                "\n"
                "Enable or disable debug mode on the fly\n");

    UniValue res(UniValue::VOBJ);

    fDebugNet = params[0].get_bool();

    res.pushKV("DebugNet", fDebugNet ? "Entering debug mode." : "Exiting debug mode.");

    return res;
}

UniValue getlistof(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getlistof <keytype>\n"
                "\n"
                "<keytype> -> key of requested data\n"
                "\n"
                "Displays data associated to a specified key type\n");

    UniValue res(UniValue::VOBJ);

    std::string sType = params[0].get_str();

    res.pushKV("Key Type", sType);

    LOCK(cs_main);

    UniValue entries(UniValue::VOBJ);
    for(const auto& entry : ReadSortedCacheSection(StringToSection(sType)))
    {
        const auto& key = entry.first;
        const auto& value = entry.second;

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("value", value.value);
        obj.pushKV("timestamp", value.timestamp);
        entries.pushKV(key, obj);
    }

    res.pushKV("entries", entries);
    return res;
}

UniValue listdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "listdata <keytype>\n"
                "\n"
                "<keytype> -> key in cache\n"
                "\n"
                "Displays data associated to a key stored in cache\n");

    UniValue res(UniValue::VOBJ);

    std::string sType = params[0].get_str();

    res.pushKV("Key Type", sType);

    LOCK(cs_main);

    Section section = StringToSection(sType);
    for(const auto& item : ReadCacheSection(section))
        res.pushKV(item.first, item.second.value);

    return res;
}

UniValue listprojects(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "listprojects\n"
                "\n"
                "Displays information about whitelisted projects.\n");

    UniValue res(UniValue::VOBJ);

    for (const auto& project : NN::GetWhitelist().Snapshot().Sorted()) {
        UniValue entry(UniValue::VOBJ);

        entry.pushKV("display_name", project.DisplayName());
        entry.pushKV("url", project.m_url);
        entry.pushKV("base_url", project.BaseUrl());
        entry.pushKV("display_url", project.DisplayUrl());
        entry.pushKV("stats_url", project.StatsUrl());
        entry.pushKV("time", DateTimeStrFormat(project.m_timestamp));

        res.pushKV(project.m_name, entry);
    }

    return res;
}

UniValue memorizekeys(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "memorizekeys\n"
                "\n"
                "Runs a full table scan of Load Admin Messages\n");

    UniValue res(UniValue::VOBJ);

    std::string sOut;

    LOCK(cs_main);

    LoadAdminMessages(true, sOut);

    res.pushKV("Results", sOut);

    return res;
}

UniValue network(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "network\n"
                "\n"
                "Display information about the network health\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    const NN::NetworkStats stats = NN::Tally::GetNetworkStats();
    const double money_supply = DoubleFromAmount(pindexBest->nMoneySupply);
    const double interest_percent = stats.m_average_daily_block_subsidy * 365 / (money_supply + 0.01);

    res.pushKV("Network Total Magnitude", (int)stats.m_total_magnitude);
    res.pushKV("Network Average Magnitude", stats.m_average_magnitude);
    res.pushKV("Network Avg Daily Payments", stats.m_average_daily_research_subsidy);
    res.pushKV("Network Max Daily Payments", stats.m_max_daily_research_subsidy);
    res.pushKV("Network Interest Paid (14 days)", stats.m_two_week_block_subsidy);
    res.pushKV("Network Avg Daily Interest", stats.m_average_daily_block_subsidy);
    res.pushKV("Total Money Supply", money_supply);
    res.pushKV("Network Interest %", interest_percent);
    res.pushKV("Magnitude Unit (GRC payment per Magnitude per day)", stats.m_magnitude_unit);

    return res;
}

UniValue projects(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "projects\n"
                "\n"
                "Displays information on projects in the network as well as researcher data if available\n");

    UniValue res(UniValue::VARR);
    NN::ResearcherPtr researcher = NN::Researcher::Get();

    for (const auto& item : NN::GetWhitelist().Snapshot().Sorted())
    {
        UniValue entry(UniValue::VOBJ);

        entry.pushKV("Project", item.DisplayName());
        entry.pushKV("URL", item.DisplayUrl());

        if (const NN::ProjectOption project = researcher->Project(item.m_name)) {
            UniValue researcher(UniValue::VOBJ);

            researcher.pushKV("CPID", project->m_cpid.ToString());
            researcher.pushKV("Team", project->m_team);
            researcher.pushKV("Valid for Research", project->Eligible());

            if (!project->Eligible()) {
                researcher.pushKV("Errors", project->ErrorMessage());
            }

            entry.pushKV("Researcher", researcher);
        }

        res.push_back(entry);
    }

    return res;
}

UniValue readconfig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "readconfig\n"
                "\n"
                "Re-reads config file; Does not overwrite pre-existing loaded values\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    ReadConfigFile(mapArgs, mapMultiArgs);

    res.pushKV("readconfig", 1);

    return res;
}

UniValue readdata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "readdata <key>\n"
                "\n"
                "<key> -> generic key\n"
                "\n"
                "Reads generic data from disk from a specified key\n");

    UniValue res(UniValue::VOBJ);

    std::string sKey = params[0].get_str();
    std::string sValue = "?";

    //CTxDB txdb("cr");
    CTxDB txdb;

    if (!txdb.ReadGenericData(sKey, sValue))
    {
        res.pushKV("Error", sValue);

        sValue = "Failed to read from disk.";
    }

    res.pushKV("Key", sKey);
    res.pushKV("Result", sValue);

    return res;
}

UniValue refhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "refhash <walletaddress>\n"
                "\n"
                "<walletaddress> -> GRC address to test against\n"
                "\n"
                "Tests to see if a GRC Address is a participant in neural network along with default wallet address\n");

    UniValue res(UniValue::VOBJ);

    std::string rh = params[0].get_str();
    bool r1 = StrLessThanReferenceHash(rh);
    bool r2 = IsNeuralNodeParticipant(DefaultWalletAddress(), GetAdjustedTime());

    res.pushKV("<Ref Hash", r1);

    res.pushKV("WalletAddress<Ref Hash", r2);

    return res;
}

UniValue sendblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "sendblock <blockhash>\n"
                "\n"
                "<blockhash> Blockhash of block to send to network\n"
                "\n"
                "Sends a block to the network\n");

    UniValue res(UniValue::VOBJ);

    uint256 hash = uint256S(params[0].get_str());
    bool fResult = AskForOutstandingBlocks(hash);

    res.pushKV("Requesting", hash.ToString());
    res.pushKV("Result", fResult);

    return res;
}

UniValue sendrawcontract(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "sendrawcontract <contract>\n"
                "\n"
                "<contract> -> custom contract\n"
                "\n"
                "Send a raw contract in a transaction on the network\n");

    UniValue res(UniValue::VOBJ);

    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    std::string sAddress = GetBurnAddress();

    CBitcoinAddress address(sAddress);

    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Gridcoin address");

    std::string sContract = params[0].get_str();
    int64_t nAmount = CENT;
    // Wallet comments
    CWalletTx wtx;
    wtx.hashBoinc = sContract;

    string strError = pwalletMain->SendMoneyToDestination(address.Get(), nAmount, wtx, false);

    if (!strError.empty())
        throw JSONRPCError(RPC_WALLET_ERROR, strError);

    res.pushKV("Contract", sContract);
    res.pushKV("Recipient", sAddress);
    res.pushKV("TrxID", wtx.GetHash().GetHex());

    return res;
}

UniValue superblockaverage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "superblockaverage\n"
                "\n"
                "Displays average information for current superblock\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    const NN::SuperblockPtr superblock = NN::Tally::CurrentSuperblock();

    res.pushKV("beacon_count", (uint64_t)superblock->m_cpids.TotalCount());
    res.pushKV("beacon_participant_count", (uint64_t)superblock->m_cpids.size());
    res.pushKV("average_magnitude", superblock->m_cpids.AverageMagnitude());
    res.pushKV("superblock_valid", superblock->WellFormed());
    res.pushKV("Superblock Age", superblock->Age());
    res.pushKV("Dire Need of Superblock", NN::Tally::SuperblockNeeded());

    return res;
}

UniValue tally(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "tally\n"
                "\n"
                "Requests a tally of research averages\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    const CBlockIndex* tallyIndex = NN::Tally::FindTrigger(pindexBest);
    NN::Tally::LegacyRecount(tallyIndex);

    res.pushKV("Tally Network Averages", 1);

    return res;
}

UniValue tallyneural(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "tallyneural\n"
                "\n"
                "Requests a tally of neural network\n");

    UniValue res(UniValue::VOBJ);

    LOCK(cs_main);

    ComputeNeuralNetworkSupermajorityHashes();
    res.pushKV("Ready", ".");

    return res;
}

UniValue versionreport(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "versionreport\n"
                "\n"
                "Displays a report on various versions recently stake on the network\n");

    LOCK(cs_main);

    UniValue myNeuralJSON = GetJSONVersionReport();

    return myNeuralJSON;
}

UniValue writedata(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
                "writedata <key> <value>\n"
                "\n"
                "<key> ---> Key where value will be written\n"
                "<value> -> Value to be written to specified key\n"
                "\n"
                "Writes a value to specified key\n");

    UniValue res(UniValue::VOBJ);

    std::string sKey = params[0].get_str();
    std::string sValue = params[1].get_str();
    //CTxDB txdb("rw");
    CTxDB txdb;
    txdb.TxnBegin();
    std::string result = "Success.";

    if (!txdb.WriteGenericData(sKey, sValue))
        result = "Unable to write.";

    if (!txdb.TxnCommit())
        result = "Unable to Commit.";

    res.pushKV("Result", result);

    return res;
}

// Network RPC commands

UniValue askforoutstandingblocks(const UniValue& params, bool fHelp)
        {
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "askforoutstandingblocks\n"
                "\n"
                "Requests network for outstanding blocks\n");

    UniValue res(UniValue::VOBJ);

    bool fResult = AskForOutstandingBlocks(uint256());

    res.pushKV("Sent.", fResult);

    return res;
}

UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getblockchaininfo\n"
                "\n"
                "Displays data on current blockchain\n");

    LOCK(cs_main);

    UniValue res(UniValue::VOBJ), diff(UniValue::VOBJ);

    res.pushKV("blocks",          nBestHeight);
    res.pushKV("moneysupply",     ValueFromAmount(pindexBest->nMoneySupply));
    diff.pushKV("proof-of-work",  GetDifficulty());
    diff.pushKV("proof-of-stake", GetDifficulty(GetLastBlockIndex(pindexBest, true)));
    res.pushKV("difficulty",      diff);
    res.pushKV("testnet",         fTestNet);
    res.pushKV("errors",          GetWarnings("statusbar"));

    return res;
}


UniValue currenttime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "currenttime\n"
                "\n"
                "Displays UTC Unix time as well as date and time in UTC\n");

    UniValue res(UniValue::VOBJ);

    res.pushKV("Unix", GetAdjustedTime());
    res.pushKV("UTC", TimestampToHRDate(GetAdjustedTime()));

    return res;
}

UniValue memorypool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "memorypool\n"
                "\n"
                "Displays included and excluded memory pool txs\n");

    UniValue res(UniValue::VOBJ);

    res.pushKV("Excluded Tx", msMiningErrorsExcluded);
    res.pushKV("Included Tx", msMiningErrorsIncluded);

    return res;
}

UniValue networktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "networktime\n"
                "\n"
                "Displays current network time\n");

    UniValue res(UniValue::VOBJ);

    res.pushKV("Network Time", GetAdjustedTime());

    return res;
}

UniValue execute(const UniValue& params, bool fHelp)
{
    throw JSONRPCError(RPC_DEPRECATED, "execute function has been deprecated; run the command as previously done so but without execute");
}

UniValue SuperblockReport(int lookback, bool displaycontract, std::string cpid)
{
    UniValue results(UniValue::VARR);
    UniValue c(UniValue::VOBJ);
    std::string Narr = ToString(GetAdjustedTime());
    c.pushKV("SuperBlock Report (14 days)",Narr);
    if (!cpid.empty())      c.pushKV("CPID",cpid);

    results.push_back(c);

    int nMaxDepth = nBestHeight;
    int nLookback = BLOCKS_PER_DAY * lookback;
    int nMinDepth = (nMaxDepth - nLookback) - ( (nMaxDepth-nLookback) % BLOCK_GRANULARITY);
    //int iRow = 0;
    CBlockIndex* pblockindex = pindexBest;
    while (pblockindex->nHeight > nMaxDepth)
    {
        if (!pblockindex || !pblockindex->pprev || pblockindex == pindexGenesisBlock) return results;
        pblockindex = pblockindex->pprev;
    }

    while (pblockindex->nHeight > nMinDepth)
    {
        if (!pblockindex || !pblockindex->pprev) return results;
        pblockindex = pblockindex->pprev;
        if (pblockindex == pindexGenesisBlock) return results;
        if (!pblockindex->IsInMainChain()) continue;
        if (IsSuperBlock(pblockindex))
        {
            const NN::ClaimOption claim = GetClaimByIndex(pblockindex);

            if (claim && claim->ContainsSuperblock())
            {
                const NN::Superblock& superblock = claim->m_superblock;

                UniValue c(UniValue::VOBJ);
                c.pushKV("Block #" + ToString(pblockindex->nHeight),pblockindex->GetBlockHash().GetHex());
                c.pushKV("Date",TimestampToHRDate(pblockindex->nTime));
                c.pushKV("Average Mag", superblock.m_cpids.AverageMagnitude());
                c.pushKV("Wallet Version", claim->m_client_version);

                if (const NN::CpidOption cpid_parsed = NN::MiningId::Parse(cpid).TryCpid())
                {
                    c.pushKV("Magnitude", superblock.m_cpids.MagnitudeOf(*cpid_parsed));
                }

                if (displaycontract)
                    c.pushKV("Contract Contents", SuperblockToJson(superblock));

                results.push_back(c);
            }
        }
    }

    return results;
}

UniValue MagnitudeReport(const NN::Cpid cpid)
{
    UniValue json(UniValue::VOBJ);

    const int64_t now = OutOfSyncByAge() ? pindexBest->nTime : GetAdjustedTime();
    const NN::ResearchAccount& account = NN::Tally::GetAccount(cpid);
    const NN::AccrualComputer calc = NN::Tally::GetComputer(cpid, now, pindexBest);

    json.pushKV("CPID", cpid.ToString());
    json.pushKV("Magnitude (Last Superblock)", account.m_magnitude);
    json.pushKV("Current Magnitude Unit", calc->MagnitudeUnit());

    json.pushKV("First Payment Time", TimestampToHRDate(account.m_first_payment_time));

    json.pushKV("Last Payment Time", TimestampToHRDate(account.LastRewardTime()));
    json.pushKV("Last Block Paid", account.LastRewardBlockHash().ToString());
    json.pushKV("Last Height Paid", (int)account.LastRewardHeight());

    json.pushKV("Accrual Days", calc->AccrualDays(account));
    json.pushKV("Owed", FormatMoney(calc->Accrual(account)));

    if (fDebug) {
        json.pushKV("Owed (raw)", FormatMoney(calc->RawAccrual(account)));
    }

    json.pushKV("Expected Earnings (14 days)", calc->ExpectedDaily(account) * 14);
    json.pushKV("Expected Earnings (Daily)", calc->ExpectedDaily(account));

    json.pushKV("Lifetime Stake Paid", account.m_total_block_subsidy);
    json.pushKV("Lifetime Research Paid", account.m_total_research_subsidy);
    json.pushKV("Lifetime Magnitude Sum", (int)account.m_total_magnitude);
    json.pushKV("Lifetime Magnitude Average", account.AverageLifetimeMagnitude());
    json.pushKV("Lifetime Payments", (int)account.m_accuracy);

    json.pushKV("Lifetime Payments Per Day", calc->PaymentPerDay(account));
    json.pushKV("Lifetime Payments Per Day Limit", calc->PaymentPerDayLimit(account));

    return json;
}

double DoubleFromAmount(int64_t amount)
{
    return (double)amount / (double)COIN;
}

UniValue GetJsonUnspentReport()
{
    // The purpose of this report is to list the details of unspent coins in the wallet, create a signed XML payload and then audit those coins as a third party
    // Written on 5-28-2017 - R HALFORD
    // We can use this as the basis for proving the total coin balance, and the current researcher magnitude in the voting system.
    UniValue results(UniValue::VARR);
    const NN::MiningId mining_id = NN::Researcher::Get()->Id();

    //Retrieve the historical magnitude
    if (const NN::CpidOption cpid = mining_id.TryCpid())
    {
        const CBlockIndex* pHistorical = GetHistoricalMagnitude(mining_id);
        UniValue entry1(UniValue::VOBJ);
        entry1.pushKV("Researcher Magnitude",pHistorical->nMagnitude);
        results.push_back(entry1);

        // Create the XML Magnitude Payload
        if (pHistorical->nHeight > 1 && pHistorical->nMagnitude > 0)
        {
            std::string sBlockhash = pHistorical->GetBlockHash().GetHex();
            std::string sError;
            std::string sSignature;
            bool bResult = SignBlockWithCPID(cpid->ToString(), pHistorical->GetBlockHash().GetHex(), sSignature, sError);
            // Just because below comment it'll keep in line with that
            if (!bResult)
                sSignature = sError;

            // Find the Magnitude from the last staked block, within the last 6 months, and ensure researcher has a valid current beacon (if the beacon is expired, the signature contain an error message)

            std::string sMagXML = "<CPID>" + cpid->ToString() + "</CPID><INNERMAGNITUDE>" + RoundToString(pHistorical->nMagnitude,2) + "</INNERMAGNITUDE>" +
                                  "<HEIGHT>" + ToString(pHistorical->nHeight) + "</HEIGHT><BLOCKHASH>" + sBlockhash + "</BLOCKHASH><SIGNATURE>" + sSignature + "</SIGNATURE>";
            std::string sMagnitude = ExtractXML(sMagXML,"<INNERMAGNITUDE>","</INNERMAGNITUDE>");
            std::string sXmlSigned = ExtractXML(sMagXML,"<SIGNATURE>","</SIGNATURE>");
            std::string sXmlBlockHash = ExtractXML(sMagXML,"<BLOCKHASH>","</BLOCKHASH>");
            std::string sXmlCPID = ExtractXML(sMagXML,"<CPID>","</CPID>");
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("CPID Signature", sSignature);
            entry.pushKV("Historical Magnitude Block #", pHistorical->nHeight);
            entry.pushKV("Historical Blockhash", sBlockhash);
            // Prove the magnitude from a 3rd party standpoint:
            if (!sXmlBlockHash.empty() && !sMagnitude.empty() && !sXmlSigned.empty())
            {
                CBlockIndex* pblockindexMagnitude = mapBlockIndex[uint256S(sXmlBlockHash)];
                if (pblockindexMagnitude)
                {
                    bool fResult = VerifyCPIDSignature(sXmlCPID, sXmlBlockHash, sXmlSigned);
                    entry.pushKV("Historical Magnitude",pblockindexMagnitude->nMagnitude);
                    entry.pushKV("Signature Valid",fResult);
                    bool fAudited = (RoundFromString(RoundToString(pblockindexMagnitude->nMagnitude,2),0)==RoundFromString(sMagnitude,0) && fResult);
                    entry.pushKV("Magnitude Audited",fAudited);
                    results.push_back(entry);
                }
            }


        }


    }

    // Now we move on to proving the coins we own are ours

    vector<COutput> vecOutputs;
    pwalletMain->AvailableCoins(vecOutputs, false, NULL, true);
    std::string sXML = "";
    std::string sRow = "";
    double dTotal = 0;
    double dBloatThreshhold = 100;
    double dCurrentItemCount = 0;
    double dItemBloatThreshhold = 50;
    // Iterate unspent coins from transactions owned by me that total over 100GRC (this prevents XML bloat)
    for (auto const& out : vecOutputs)
    {
        int64_t nValue = out.tx->vout[out.i].nValue;
        const CScript& pk = out.tx->vout[out.i].scriptPubKey;
        UniValue entry(UniValue::VOBJ);
        CTxDestination address;
        if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, address))
        {
            if (CoinToDouble(nValue) > dBloatThreshhold)
            {
                entry.pushKV("TXID", out.tx->GetHash().GetHex());
                entry.pushKV("Address", CBitcoinAddress(address).ToString());
                std::string sScriptPubKey1 = HexStr(pk.begin(), pk.end());
                entry.pushKV("Amount",ValueFromAmount(nValue));
                std::string strAddress=CBitcoinAddress(address).ToString();
                CKeyID keyID;
                const CBitcoinAddress& bcAddress = CBitcoinAddress(address);
                if (bcAddress.GetKeyID(keyID))
                {
                    bool IsCompressed;
                    CKey vchSecret;
                    if (pwalletMain->GetKey(keyID, vchSecret))
                    {
                        // Here we use the secret key to sign the coins, then we abandon the key.
                        CSecret csKey = vchSecret.GetSecret(IsCompressed);
                        CKey keyInner;
                        keyInner.SetSecret(csKey,IsCompressed);
                        std::string private_key = CBitcoinSecret(csKey,IsCompressed).ToString();
                        std::string public_key = HexStr(keyInner.GetPubKey().Raw());
                        std::vector<unsigned char> vchSig;
                        keyInner.Sign(out.tx->GetHash(), vchSig);
                        // Sign the coins we own
                        std::string sSig = std::string(vchSig.begin(), vchSig.end());
                        // Increment the total balance weight voting ability
                        dTotal += CoinToDouble(nValue);
                        sRow = "<ROW><TXID>" + out.tx->GetHash().GetHex() + "</TXID>" +
                               "<AMOUNT>" + RoundToString(CoinToDouble(nValue),2) + "</AMOUNT>" +
                               "<POS>" + RoundToString((double)out.i,0) + "</POS>" +
                               "<PUBKEY>" + public_key + "</PUBKEY>" +
                               "<SCRIPTPUBKEY>" + sScriptPubKey1  + "</SCRIPTPUBKEY>" +
                               "<SIG>" + EncodeBase64(sSig) + "</SIG>" +
                               "<MESSAGE></MESSAGE></ROW>";
                        sXML += sRow;
                        dCurrentItemCount++;
                        if (dCurrentItemCount >= dItemBloatThreshhold)
                            break;
                    }

                }
                results.push_back(entry);
            }
        }
    }

    // Now we will need to go back through the XML and Audit the claimed vote weight balance as a 3rd party

    double dCounted = 0;

    std::vector<std::string> vXML= split(sXML.c_str(),"<ROW>");
    for (unsigned int x = 0; x < vXML.size(); x++)
    {
        // Prove the contents of the XML as a 3rd party
        CTransaction tx2;
        uint256 hashBlock;
        uint256 uTXID = uint256S(ExtractXML(vXML[x],"<TXID>","</TXID>"));
        std::string sAmt = ExtractXML(vXML[x],"<AMOUNT>","</AMOUNT>");
        std::string sPos = ExtractXML(vXML[x],"<POS>","</POS>");
        std::string sXmlSig = ExtractXML(vXML[x],"<SIG>","</SIG>");
        std::string sXmlMsg = ExtractXML(vXML[x],"<MESSAGE>","</MESSAGE>");
        std::string sScriptPubKeyXml = ExtractXML(vXML[x],"<SCRIPTPUBKEY>","</SCRIPTPUBKEY>");

        int32_t iPos = RoundFromString(sPos,0);
        std::string sPubKey = ExtractXML(vXML[x],"<PUBKEY>","</PUBKEY>");

        if (!sPubKey.empty() && !sAmt.empty() && !sPos.empty() && !uTXID.IsNull())
        {

            if (GetTransaction(uTXID, tx2, hashBlock))
            {
                if (iPos >= 0 && iPos < (int32_t) tx2.vout.size())
                {
                    int64_t nValue2 = tx2.vout[iPos].nValue;
                    const CScript& pk2 = tx2.vout[iPos].scriptPubKey;
                    CTxDestination address2;
                    std::string sVotedPubKey = HexStr(pk2.begin(), pk2.end());
                    std::string sVotedGRCAddress = CBitcoinAddress(address2).ToString();
                    std::string sCoinOwnerAddress = PubKeyToAddress(pk2);
                    double dAmount = CoinToDouble(nValue2);
                    if (ExtractDestination(tx2.vout[iPos].scriptPubKey, address2))
                    {
                        if (sScriptPubKeyXml == sVotedPubKey && RoundToString(dAmount,2) == sAmt)
                        {
								UniValue entry(UniValue::VOBJ);
      					   		entry.pushKV("Audited Amount",ValueFromAmount(nValue2));
                            std::string sDecXmlSig = DecodeBase64(sXmlSig);
                            CKey keyVerify;
                            if (keyVerify.SetPubKey(ParseHex(sPubKey)))
                            {
                                std::vector<unsigned char> vchMsg1 = vector<unsigned char>(sXmlMsg.begin(), sXmlMsg.end());
                                std::vector<unsigned char> vchSig1 = vector<unsigned char>(sDecXmlSig.begin(), sDecXmlSig.end());
                                bool bValid = keyVerify.Verify(uTXID,vchSig1);
                                // Unspent Balance is proven to be owned by the voters public key, count the vote
                                if (bValid) dCounted += dAmount;
										entry.pushKV("Verified",bValid);
                            }

                            results.push_back(entry);
                        }
                    }
                }
            }
        }
    }

	UniValue entry(UniValue::VOBJ);
    // Note that the voter needs to have the wallet at least unlocked for staking in order for the coins to be signed, otherwise the coins-owned portion of the vote balance will be 0.
    // In simpler terms: The wallet must be unlocked to cast a provable vote.

	entry.pushKV("Total Voting Balance Weight", dTotal);
    entry.pushKV("Grand Verified Amount",dCounted);

    std::string sBalCheck2 = GetProvableVotingWeightXML();
    double dVerifiedBalance = ReturnVerifiedVotingBalance(sBalCheck2,true);
    double dVerifiedMag = ReturnVerifiedVotingMagnitude(sBalCheck2, true);
	entry.pushKV("Balance check",dVerifiedBalance);
	entry.pushKV("Mag check",dVerifiedMag);
    results.push_back(entry);
    return results;
}

UniValue GetUpgradedBeaconReport()
{
    UniValue results(UniValue::VARR);
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("Report","Upgraded Beacon Report 1.0");
    std::string rows = "";
    std::string row = "";
    int iBeaconCount = 0;
    int iUpgradedBeaconCount = 0;
    for(const auto& item : ReadSortedCacheSection(Section::BEACON))
    {
        const AppCacheEntry& entry = item.second;
        std::string contract = DecodeBase64(entry.value);
        std::string cpidv2 = ExtractValue(contract,";",0);
        std::string grcaddress = ExtractValue(contract,";",2);
        std::string sPublicKey = ExtractValue(contract,";",3);
        if (!sPublicKey.empty()) iUpgradedBeaconCount++;
        iBeaconCount++;
    }

    entry.pushKV("Total Beacons", iBeaconCount);
    entry.pushKV("Upgraded Beacon Count", iUpgradedBeaconCount);
    double dPct = ((double)iUpgradedBeaconCount / ((double)iBeaconCount) + .01);
    entry.pushKV("Pct Of Upgraded Beacons",RoundToString(dPct*100,3));
    results.push_back(entry);
    return results;
}

UniValue GetJSONBeaconReport()
{
    UniValue results(UniValue::VARR);
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("CPID","GRCAddress");
    std::string row;
    for(const auto& item : ReadSortedCacheSection(Section::BEACON))
    {
        const std::string& key = item.first;
        const AppCacheEntry& cache = item.second;
        row = key + "<COL>" + cache.value;
        std::string contract = DecodeBase64(cache.value);
        std::string grcaddress = ExtractValue(contract,";",2);
        entry.pushKV(key, grcaddress);
    }

    results.push_back(entry);
    return results;
}


double GetTotalNeuralNetworkHashVotes()
{
    double total = 0;
    
    // Copy to a sorted map.
    std::map<std::string, double> sorted_hashes(
                mvNeuralNetworkHash.begin(),
                mvNeuralNetworkHash.end());
    
    for(auto& entry : sorted_hashes)
    {
        auto& neural_hash = entry.first;
        auto& popularity = entry.second;

        // d41d8 is the hash of an empty magnitude contract - don't count it
        if(popularity >= 0.01 &&
           neural_hash != "TOTAL_VOTES" &&
           neural_hash != "d41d8cd98f00b204e9800998ecf8427e")
        {
            total += popularity;
        }
    }
    
    return total;
}


double GetTotalCurrentNeuralNetworkHashVotes()
{
    double total = 0;
    
    // Copy to a sorted map.
    std::map<std::string, double> sorted_hashes(
                mvCurrentNeuralNetworkHash.begin(),
                mvCurrentNeuralNetworkHash.end());
    
    for(auto& entry : sorted_hashes)
    {
        auto& neural_hash = entry.first;
        auto& popularity = entry.second;
        
        // d41d8 is the hash of an empty magnitude contract - don't count it
        if(popularity >= .01 &&
           neural_hash != "TOTAL_VOTES" &&
           neural_hash != "d41d8cd98f00b204e9800998ecf8427e")
        {
            total += popularity;
        }
    }
    
    return total;
}


UniValue GetJSONNeuralNetworkReport()
{
    UniValue results(UniValue::VARR);
    //Returns a report of the networks neural hashes in order of popularity
    std::string report = "Neural_hash, Popularity\n";
    std::string row = "";
    double pct = 0;
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("Neural Hash","Popularity,Percent %");
    double votes = GetTotalNeuralNetworkHashVotes();

    // Copy to a sorted map.
    std::map<std::string, double> sorted_hashes(
                mvNeuralNetworkHash.begin(),
                mvNeuralNetworkHash.end());
    
    for(auto& hash : sorted_hashes)
    {
        auto& neural_hash = hash.first;
        auto& popularity = hash.second;
        
        //If the hash != empty_hash: >= .01
        if (popularity > 0 &&
            neural_hash != "TOTAL_VOTES" &&
            neural_hash != "d41d8cd98f00b204e9800998ecf8427e")
        {
            row = neural_hash + "," + RoundToString(popularity,0);
            report += row + "\n";
            pct = (((double)popularity)/(votes+.01))*100;
            entry.pushKV(neural_hash,RoundToString(popularity,0) + "; " + RoundToString(pct,2) + "%");
        }
    }
    // If we have a pending superblock, append it to the report:
    std::string SuperblockHeight = ReadCache(Section::NEURALSECURITY, "pending").value;
    if (!SuperblockHeight.empty() && SuperblockHeight != "0")
    {
        entry.pushKV("Pending",SuperblockHeight);
    }

    const int64_t superblock_age = NN::Tally::CurrentSuperblock()->Age();

    entry.pushKV("Superblock Age",superblock_age);
    if (superblock_age > GetSuperblockAgeSpacing(nBestHeight))
    {
        int iRoot = 30;
        int iModifier = (nBestHeight % iRoot);
        int iQuorumModifier = (nBestHeight % 10);
        int iLastNeuralSync = nBestHeight - iModifier;
        int iNextNeuralSync = iLastNeuralSync + iRoot;
        int iLastQuorum = nBestHeight - iQuorumModifier;
        int iNextQuorum = iLastQuorum + 10;
        entry.pushKV("Last Sync", iLastNeuralSync);
        entry.pushKV("Next Sync", iNextNeuralSync);
        entry.pushKV("Next Quorum", iNextQuorum);
    }
    results.push_back(entry);
    return results;
}


UniValue GetJSONCurrentNeuralNetworkReport()
{
    UniValue results(UniValue::VARR);
    //Returns a report of the networks neural hashes in order of popularity
    std::string neural_hash = "";
    std::string report = "Neural_hash, Popularity\n";
    std::string row = "";
    double pct = 0;
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("Neural Hash","Popularity,Percent %");
    double votes = GetTotalCurrentNeuralNetworkHashVotes();

    // Copy to a sorted map.
    std::map<std::string, double> sorted_hashes(
                mvCurrentNeuralNetworkHash.begin(),
                mvCurrentNeuralNetworkHash.end());
    
    for(auto& hash : sorted_hashes)
    {
        auto& neural_hash = hash.first;
        auto& popularity = hash.second;
        
        //If the hash != empty_hash: >= .01
        if(popularity > 0 &&
           neural_hash != "TOTAL_VOTES" &&
           neural_hash != "d41d8cd98f00b204e9800998ecf8427e")
        {
            row = neural_hash + "," + RoundToString(popularity,0);
            report += row + "\n";
            pct = (((double)popularity)/(votes+.01))*100;
            entry.pushKV(neural_hash,RoundToString(popularity,0) + "; " + RoundToString(pct,2) + "%");
        }
    }
    
    // If we have a pending superblock, append it to the report:
    std::string SuperblockHeight = ReadCache(Section::NEURALSECURITY, "pending").value;
    if (!SuperblockHeight.empty() && SuperblockHeight != "0")
    {
        entry.pushKV("Pending",SuperblockHeight);
    }

    const int64_t superblock_age = NN::Tally::CurrentSuperblock()->Age();

    entry.pushKV("Superblock Age",superblock_age);
    if (superblock_age > GetSuperblockAgeSpacing(nBestHeight))
    {
        int iRoot = 30;
        int iModifier = (nBestHeight % iRoot);
        int iQuorumModifier = (nBestHeight % 10);
        int iLastNeuralSync = nBestHeight - iModifier;
        int iNextNeuralSync = iLastNeuralSync + iRoot;
        int iLastQuorum = nBestHeight - iQuorumModifier;
        int iNextQuorum = iLastQuorum + 10;
        entry.pushKV("Last Sync", iLastNeuralSync);
        entry.pushKV("Next Sync", iNextNeuralSync);
        entry.pushKV("Next Quorum", iNextQuorum);
    }
    results.push_back(entry);
    return results;
}


UniValue GetJSONVersionReport()
{
    UniValue results(UniValue::VARR);
    //Returns a report of the GRC Version staking blocks over the last 100 blocks
    std::string report = "Version, Popularity\n";
    std::string row = "";
    double pct = 0;
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("Version","Popularity,Percent %");
    
    double votes = 0;
    for(auto it : mvNeuralVersion)
        votes += it.second;
    
    // Copy to a sorted map.
    std::map<std::string, double> sorted_versions(
                mvNeuralVersion.begin(),
                mvNeuralVersion.end());
    
    for(auto& version : sorted_versions)
    {
        auto& neural_ver = version.first;
        auto& popularity = version.second;        
        
        //If the hash != empty_hash:
        if (popularity > 0)
        {
            row = neural_ver + "," + RoundToString(popularity,0);
            report += row + "\n";
            pct = popularity/(votes+.01)*100;
            entry.pushKV(neural_ver,RoundToString(popularity,0) + "; " + RoundToString(pct,2) + "%");
        }
    }
    
    results.push_back(entry);
    return results;
}

std::string YesNo(bool f)
{
    return f ? "Yes" : "No";
}

UniValue listitem(const UniValue& params, bool fHelp)
{
    throw JSONRPCError(RPC_DEPRECATED, "list is deprecated; Please run the command the same as previously without list");
}

// ppcoin: get information of sync-checkpoint
UniValue getcheckpoint(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getcheckpoint\n"
                "Show info of synchronized checkpoint.\n");

    UniValue result(UniValue::VOBJ);

    LOCK(cs_main);

    const CBlockIndex* pindexCheckpoint = Checkpoints::GetLastCheckpoint(mapBlockIndex);
    if(pindexCheckpoint != NULL)
    {
        result.pushKV("synccheckpoint", pindexCheckpoint->GetBlockHash().ToString().c_str());
        result.pushKV("height", pindexCheckpoint->nHeight);
        result.pushKV("timestamp", DateTimeStrFormat(pindexCheckpoint->GetBlockTime()).c_str());
    }

    return result;
}

//Brod
UniValue rpc_reorganize(const UniValue& params, bool fHelp)
{
    UniValue results(UniValue::VOBJ);
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "reorganize <hash>\n"
                "Roll back the block chain to specified block hash.\n"
                "The block hash must already be present in block index");

    uint256 NewHash;
    NewHash.SetHex(params[0].get_str());

    bool fResult = ForceReorganizeToHash(NewHash);
    results.pushKV("RollbackChain",fResult);
    return results;
}
