#ifndef HANDLE_JSON_H
#define HANDLE_JSON_H

// CPP
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <set>

// Crypto++
#include <cryptopp/aes.h> // Default params key

// JSON
#include "json.hpp"

///////////////////////////////////////
////////     CONFIGURATION     ////////
///////////////////////////////////////

struct Configuration
{
    std::string outputFolder;
    std::string meshPath;
    int nbBytesKey; // 16 bytes by default (AES 128) or 32 bytes (AES 256)
    int nbBytesIV;  // 16 bytes by default (AES 128) or 32 bytes (AES 256)
    int nbAttempts;
    bool saveOrig, saveQP, saveEnc, saveDec;
    bool plotOrig, plotQP, plotEnc, plotDec;

    // --- QUANTIZATION PARAMETER --- //
    int qp;

    // --- DEFORMATION PARAMS (funcEncMantissa) --- //
    int nbBitsEnc, nbClearLSB;

    // --- DEFORMATION PARAMS (funcDefAxis) --- //
    // ranges [min,max] on [0,1] + idBand < 1.0 --> By symetry, deformation will also have 1/X
    float minRange, maxRange, idBand;
    struct MonotonicParams
    {
        float aMin, aMax, bMin, bMax;
    } mono;
    struct OscillatorParams
    {
        float f0Min, f0Max, f1Cycles, dMax;
    } osci;

    void printConfig() const;
};

///////////////////////////////////////
////////         JSON         /////////
///////////////////////////////////////

void handleBoolJSON(const nlohmann::json &j, const std::string &key, bool &output, bool defaultValue = false);
void handleStringJSON(const nlohmann::json &j, const std::string &key, std::string &output, const std::string &defaultValue = "");
void handleFloatJSON(const nlohmann::json &j, const std::string &key, float &output, float defaultValue = 0.0);
void handleIntJSON(const nlohmann::json &j, const std::string &key, int &output, int defaultValue = 0);

void from_json(const nlohmann::json &j, Configuration &config);
void readJsonFile(const std::string &filePath, Configuration &config);

///////////////////////////////////////
////////    PROCESS  INPUT    /////////
///////////////////////////////////////

bool has_valid_mesh_ext(const std::filesystem::path &p);
std::vector<std::string> collect_mesh_paths(const std::string &root, bool recursive = true, bool follow_dir_symlinks = false);

#endif