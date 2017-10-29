#ifndef _SMAZ_H
#define _SMAZ_H

int smaz_compress(const char *in, int inlen, char *out, int outlen);
int smaz_decompress(const char *in, int inlen, char *out, int outlen);

// Gridcoin
typedef std::vector<std::pair<std::string, std::string>> CTxMessage;

void CompressTxMessage(const std::string& sMessage, std::string& sReturnCompressed);
void DecompressTxMessage(const std::string& sMessage, std::string& sReturnDecompressed);

#endif
