#pragma once

#include <string>
#include <map>
#include <vector>

void ParseParameters(int argc, const char*const argv[]);
void InterpretNegativeSetting(std::string name, std::map<std::string, std::string>& mapSettingsRet);
void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet, std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet);

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "foo"). Will be prefixed with "-".
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArgument(const std::string& strArg, const std::string& strDefault);

/**
 * Set argument value.
 *
 * @param argKey Argument to set (e.g. "foo"). Will be prefixed with "-".
 * @param argValue New argument value (e.g. "1")
 */
void SetArgument(const std::string &argKey, const std::string &argValue);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault=false);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

// Forces an arg setting. Called by SoftSetArg() if the arg hasn't already
// been set. Also called directly in testing.
void ForceSetArg(const std::string& strArg, const std::string& strValue);
