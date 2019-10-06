#ifndef UPGRADE_H
#define UPGRADE_H

#include <string>

/*
 * A Class to support update checks and well as to be built on for future use with snapshot download and apply
 *
*/
class Upgrade
{
public:
    bool CheckForLatestUpdate();


private:
    std::string githuburl = "https://api.github.com/repos/gridcoin-community/Gridcoin-Research/releases/latest";

};

#endif // UPGRADE_H
