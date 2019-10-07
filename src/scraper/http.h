#pragma once

#include <string>
#include <stdexcept>

/** Call backs are in namespace so these must be here in the header **/
extern int64_t SnapshotDownloadSpeed;
extern int64_t SnapshotDownloadProgress;
extern int64_t SnapshotDownloadSize;

//!
//! \brief HTTP exception.
//!
//! Used to signal an unexpected server response.
//!
class HttpException : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

//!
//! \brief Scraper HTTP handler.
//!
//! A rudimentary implementation of an HTTP handler used by the scraper when
//! downloading stat files or fetching ETags.
//!
//! \todo If needed this class can be exposed and refined. Alternatively it
//! can be replaced with curlpp.
//!
class Http
{
public:
    //!
    //! \brief Constructor.
    //!
    Http();

    //!
    //! \brief Destructor.
    //!
    ~Http();

    bool SnapshotDownloadComplete;

    //!
    //! \brief Download file from server.
    //!
    //! Attempts to download \p url to \p destination using optional HTTP
    //! credentials.
    //!
    //! \param url URL to download.
    //! \param destination Destination path, including filename.
    //! \param userpass Optional HTTP credentials.
    //! \throws std::runtime_error if \p destination cannot be opened.
    //!
    void Download(
            const std::string& url,
            const std::string& destination,
            const std::string& userpass = "");

    //!
    //! \brief Fetch ETag for URL.
    //!
    //! Downlaods the headers for \p url and attempts to find the ETag.
    //!
    //! \param url URL to fetch ETag from.
    //! \param userpass Optional HTTP credentials.
    //! \return ETag for \p url.
    //! \throws HttpException on invalid server response.
    //! \throws std::runtime_error if ETag cannot be found.
    //!
    std::string GetEtag(
            const std::string& url,
            const std::string& userpass = "");

    //!
    //! \brief Fetch github release information.
    //!
    //! Downloads the json data from github that contains information about latest releases.
    //!
    //! \param url URL to fetch json from.
    //! \throws HttpException on invalid server response.
    //!
    std::string GetLatestVersionResponse(
            const std::string& url);
    //!
    //! \brief Download Snapshot with progress updates.
    //!
    //! Downloads the snapshot from the latest snapshot.zip hosted on gridcoin.us.
    //!
    //! \param url URL of the snapshot.zip.
    //! \param destination Destination of the snapshot file.
    //! \throws HttpException on invalid server response.
    //!
    void DownloadSnapshot(
            const std::string& url,
            const std::string& destination);

private:
    void EvaluateResponse(int code, const std::string& url);
};
