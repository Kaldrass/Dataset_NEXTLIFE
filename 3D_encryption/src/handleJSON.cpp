#include "handleJSON.h"

///////////////////////////////////////
////////     CONFIGURATION     ////////
///////////////////////////////////////

// For printing vectors in cout
std::ostream &operator<<(std::ostream &os, const std::vector<int> &vec)
{
    os << "[";
    for (size_t i = 0; i < vec.size(); ++i)
    {
        os << vec[i];
        if (i < vec.size() - 1)
            os << ", ";
    }
    os << "]";
    return os;
}

void Configuration::printConfig() const
{
    std::cout << "[JSON] : OutputFolder : " << this->outputFolder << std::endl;
    std::cout << "[JSON] : MeshPath : " << this->meshPath << std::endl;
    std::cout << "[JSON] : NbBytesKey : " << this->nbBytesKey << std::endl;
    std::cout << "[JSON] : NbBytesIV : " << this->nbBytesIV << std::endl;
    std::cout << "[JSON] : NbAttempts : " << this->nbAttempts << std::endl;

    std::cout << "[JSON] : saveOrig : " << this->saveOrig << std::endl;
    std::cout << "[JSON] : saveEnc : " << this->saveEnc << std::endl;
    std::cout << "[JSON] : saveDec : " << this->saveDec << std::endl;

    std::cout << "[JSON] : plotOrig : " << this->plotOrig << std::endl;
    std::cout << "[JSON] : plotEnc : " << this->plotEnc << std::endl;
    std::cout << "[JSON] : plotDec : " << this->plotDec << std::endl;

    std::cout << "[JSON] : minRange : " << this->minRange << std::endl;
    std::cout << "[JSON] : maxRange : " << this->maxRange << std::endl;
    std::cout << "[JSON] : idBand : " << this->idBand << std::endl;

    std::cout << "[JSON] : f0Min : " << this->osci.f0Min << std::endl;
    std::cout << "[JSON] : f0Max : " << this->osci.f0Max << std::endl;
    std::cout << "[JSON] : f1Cycles : " << this->osci.f1Cycles << std::endl;
    std::cout << "[JSON] : dMax : " << this->osci.dMax << std::endl;

    std::cout << "[JSON] : aMin : " << this->mono.aMin << std::endl;
    std::cout << "[JSON] : aMax : " << this->mono.aMax << std::endl;
    std::cout << "[JSON] : bMin : " << this->mono.bMin << std::endl;
    std::cout << "[JSON] : bMax : " << this->mono.bMax << std::endl;

    // ...

    std::cout << std::endl;
}

///////////////////////////////////////
////////         JSON         /////////
///////////////////////////////////////

void handleBoolJSON(const nlohmann::json &j, const std::string &key, bool &output, bool defaultValue)
{
    // Key not find, default value used instead
    if (j.find(key) == j.end())
    {
        std::cout << "[WARNING] - JSON : default value used for " << key << " = " << defaultValue << std::endl;
        output = defaultValue;
        return;
    }

    // Key find, but not with the correct type
    if (!j.at(key).is_boolean())
    {
        throw std::invalid_argument("'" + key + "' is not boolean type.");
    }

    // Key find with the correct type
    output = j.at(key).get<bool>();
}
void handleStringJSON(const nlohmann::json &j, const std::string &key, std::string &output, const std::string &defaultValue)
{
    // Key not found, use default value instead
    if (j.find(key) == j.end())
    {
        std::cout << "[WARNING] - JSON : default value used for " << key << " = " << defaultValue << std::endl;
        output = defaultValue;
        return;
    }

    // Key found, but not with the correct type
    if (!j.at(key).is_string())
    {
        throw std::invalid_argument("'" + key + "' is not a string type.");
    }

    // Key found with the correct type
    output = j.at(key).get<std::string>();
}
void handleFloatJSON(const nlohmann::json &j, const std::string &key, float &output, float defaultValue)
{
    // Key not find, default value used instead
    if (j.find(key) == j.end())
    {
        std::cout << "[WARNING] - JSON : default value used for " << key << " = " << defaultValue << std::endl;
        output = defaultValue;
        return;
    }

    // Key find, but not with the correct type
    if (!j.at(key).is_number_float())
    {
        throw std::invalid_argument("'" + key + "' is not float type.");
    }

    // Key find with the correct type
    output = j.at(key).get<float>();
}
void handleIntJSON(const nlohmann::json &j, const std::string &key, int &output, int defaultValue)
{
    // Key not find, default value used instead
    if (j.find(key) == j.end())
    {
        std::cout << "[WARNING] - JSON : default value used for " << key << " = " << defaultValue << std::endl;
        output = defaultValue;
        return;
    }

    // Key find, but not with the correct type
    if (!j.at(key).is_number_integer())
    {
        throw std::invalid_argument("'" + key + "' is not int type.");
    }

    // Key find with the correct type
    output = j.at(key).get<int>();
}

void from_json(const nlohmann::json &j, Configuration &config)
{
    // Core parameters
    handleStringJSON(j, "outputFolder", config.outputFolder);
    handleStringJSON(j, "meshPath", config.meshPath);
    handleIntJSON(j, "nbBytesKey", config.nbBytesKey, CryptoPP::AES::DEFAULT_KEYLENGTH);
    handleIntJSON(j, "nbBytesIV", config.nbBytesIV, CryptoPP::AES::DEFAULT_KEYLENGTH);
    handleIntJSON(j, "nbAttempts", config.nbAttempts, 1);

    // saveOutputFile
    const nlohmann::json &saveOutputFile = j.at("save");
    handleBoolJSON(saveOutputFile, "orig", config.saveOrig);
    handleBoolJSON(saveOutputFile, "qp", config.saveQP);
    handleBoolJSON(saveOutputFile, "enc", config.saveEnc);
    handleBoolJSON(saveOutputFile, "dec", config.saveDec);

    // plotOutputFile
    const nlohmann::json &plotOutputFile = j.at("plot");
    handleBoolJSON(plotOutputFile, "orig", config.plotOrig);
    handleBoolJSON(plotOutputFile, "qp", config.plotQP);
    handleBoolJSON(plotOutputFile, "enc", config.plotEnc);
    handleBoolJSON(plotOutputFile, "dec", config.plotDec);

    // Quantization parameter
    handleIntJSON(j, "qp", config.qp, 0);

    // EncMantissaParams
    const nlohmann::json &funcEncMantissa = j.at("funcEncMantissa");
    handleIntJSON(funcEncMantissa, "nbBitsEnc", config.nbBitsEnc, 0);
    handleIntJSON(funcEncMantissa, "nbClearLSB", config.nbClearLSB, 0);

    // deformParams
    const nlohmann::json &funcDefAxis = j.at("funcDefAxis");
    // deformParams : ranges
    const nlohmann::json &ranges = funcDefAxis.at("ranges");
    handleFloatJSON(ranges, "minRange", config.minRange);
    handleFloatJSON(ranges, "maxRange", config.maxRange);
    handleFloatJSON(ranges, "idBand", config.idBand);
    // deformParams : oscillatory
    const nlohmann::json &oscillatory = funcDefAxis.at("oscillatory");
    handleFloatJSON(oscillatory, "f0Min", config.osci.f0Min);
    handleFloatJSON(oscillatory, "f0Max", config.osci.f0Max);
    handleFloatJSON(oscillatory, "f1Cycles", config.osci.f1Cycles);
    handleFloatJSON(oscillatory, "dMax", config.osci.dMax);
    // deformParams : monotonic
    const nlohmann::json &monotonic = funcDefAxis.at("monotonic");
    handleFloatJSON(monotonic, "aMin", config.mono.aMin);
    handleFloatJSON(monotonic, "aMax", config.mono.aMax);
    handleFloatJSON(monotonic, "bMin", config.mono.bMin);
    handleFloatJSON(monotonic, "bMax", config.mono.bMax);
}

void readJsonFile(const std::string &filePath, Configuration &config)
{
    try
    {
        std::ifstream jsonFile(filePath);
        if (!jsonFile.is_open())
        {
            throw std::invalid_argument("[JSON] - ERROR no JSON file found.");
        }

        nlohmann::json jsonData;
        jsonFile >> jsonData;

        // Convert JSON into struct
        config = jsonData.get<Configuration>();
    }
    catch (const std::exception &e)
    {
        throw std::runtime_error("[JSON] - ERROR while reading JSON file : " + std::string(e.what()));
    }
}

///////////////////////////////////////
////////    PROCESS  INPUT    /////////
///////////////////////////////////////

// -----------------------------------------------------------------------------
// processInit_meshList: given a path (file or directory), populate vector fullPaths
// -----------------------------------------------------------------------------

bool has_valid_mesh_ext(const std::filesystem::path &p)
{
    static const std::array<std::string, 2> exts = {".ply", ".obj"};
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c)
                   { return std::tolower(c); });
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::vector<std::string> collect_mesh_paths(const std::string &root, bool recursive, bool follow_dir_symlinks)
{
    std::vector<std::string> out;
    std::error_code ec;

    std::filesystem::path p(root);
    if (!std::filesystem::exists(p, ec))
    {
        throw std::runtime_error("Path does not exist: " + root);
    }

    auto add_if_mesh = [&](const std::filesystem::path &fp)
    {
        if (has_valid_mesh_ext(fp))
        {
            // normalize; weakly_canonical avoids throwing on some broken symlinks
            std::filesystem::path canon = std::filesystem::weakly_canonical(fp, ec);
            out.push_back((ec ? fp : canon).string());
        }
    };

    if (std::filesystem::is_regular_file(p, ec))
    {
        if (!has_valid_mesh_ext(p))
            throw std::runtime_error("Unsupported file extension: " + root);
        add_if_mesh(p);
    }
    else if (std::filesystem::is_directory(p, ec))
    {
        const auto opts = (follow_dir_symlinks ? std::filesystem::directory_options::follow_directory_symlink
                                               : std::filesystem::directory_options::none) |
                          std::filesystem::directory_options::skip_permission_denied;

        if (recursive)
        {
            for (std::filesystem::recursive_directory_iterator it(p, opts, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                    continue; // skip entries we can't stat
                const auto &e = *it;
                if (e.is_regular_file(ec))
                    add_if_mesh(e.path());
            }
        }
        else
        {
            for (std::filesystem::directory_iterator it(p, opts, ec), end; it != end; it.increment(ec))
            {
                if (ec)
                    continue;
                const auto &e = *it;
                if (e.is_regular_file(ec))
                    add_if_mesh(e.path());
            }
        }
    }
    else
    {
        throw std::runtime_error("Not a file or directory: " + root);
    }

    // Stable, deterministic order
    std::sort(out.begin(), out.end());
    // Optional: deduplicate in case of aliasing/symlinks
    out.erase(std::unique(out.begin(), out.end()), out.end());

    if (out.empty())
        throw std::runtime_error("No .ply/.obj files found under: " + root);

    return out;
}