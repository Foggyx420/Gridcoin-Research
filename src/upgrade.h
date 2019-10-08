#ifndef UPGRADE_H
#define UPGRADE_H

#include <string>
#include <memory>

#include "scraper/http.h"

/*
 * A Class to support update checks and well as to be built on for future use with snapshot download and apply
 *
*/
class Upgrade
{
public:
    Upgrade();
    /** Check Github for latest version information **/
    void CheckForLatestUpdate();
    /** Start the Snapshot Process **/
    bool SnapshotRequested();
    /** Download Snapshot **/
    void DownloadSnapshot();
private:
};

extern std::unique_ptr<Upgrade> g_UpdateChecker;
#endif // UPGRADE_H
