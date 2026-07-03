#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <set>
#include <algorithm>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/osrng.h>

#include <opencv2/opencv.hpp>

namespace cp = CryptoPP;

// --------- File helpers ----------
bool writeFile(const std::string& path, const unsigned char* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data), size);
    return true;
}

bool readFile(const std::string& path, std::vector<unsigned char>& buffer, size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    buffer.resize(size);
    in.read(reinterpret_cast<char*>(buffer.data()), size);
    return static_cast<size_t>(in.gcount()) == size;
}

// --------- CLI parsing ----------
struct Args {
    std::string op;        // "encrypt" | "decrypt"
    std::string mode;      // "full" | "bits"
    std::string bitsSpec;  // required if mode == "bits"
    std::string inPath;
    std::string outPath;
    std::string keyPath;
    std::string ivPath;
    std::string maskPath;  // optional; empty if none
};

void printUsage(const char* exe) {
    std::cerr <<
      "Usage:\n"
      "  " << exe << " --op <encrypt|decrypt> --mode <full|bits>\n"
      "               --in <input_image> --out <output_image>\n"
      "               --key <key_file> --iv <iv_file> [--mask <mask_image>]\n"
      "               [--bits <b0[,b1,...]  (required for mode=bits; 0..7)>]\n\n"
      "Notes:\n"
      "  * If --mask is provided, the operation applies only where mask pixels are white (>=128).\n"
      "  * mode=full  → byte-wise AES-CTR over all channels (like before).\n"
      "  * mode=bits  → AES-CTR over a packed stream of selected bit-planes (from each channel byte).\n"
      "                 Example: --bits 7 (MSB) or --bits 6,7 (two top bits) or --bits 0,2,5.\n"
      "  * Keys/IVs are generated on encrypt, and read back on decrypt.\n";
}

static std::vector<int> parseBitsList(const std::string& spec) {
    if (spec.empty()) throw std::runtime_error("Missing --bits for mode=bits");
    std::set<int> uniq;
    std::string cur;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == ',') {
            if (cur.empty()) throw std::runtime_error("Invalid --bits list (empty entry).");
            int v = std::stoi(cur);
            if (v < 0 || v > 7) throw std::runtime_error("--bits values must be in [0..7]");
            uniq.insert(v);
            cur.clear();
        } else if (!isspace(static_cast<unsigned char>(spec[i]))) {
            cur.push_back(spec[i]);
        }
    }
    if (uniq.empty()) throw std::runtime_error("No valid bit positions provided.");
    std::vector<int> bits(uniq.begin(), uniq.end());
    // Use descending order for deterministic packing: 7,6,...,0
    std::sort(bits.begin(), bits.end(), std::greater<int>());
    return bits;
}

Args parseArgs(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string t = argv[i];
        auto need = [&](const char* flag){
            if (i+1 >= argc) { printUsage(argv[0]); throw std::runtime_error(std::string("Missing value for ")+flag); }
            return std::string(argv[++i]);
        };
        if      (t == "--op")    a.op       = need("--op");
        else if (t == "--mode")  a.mode     = need("--mode");
        else if (t == "--in")    a.inPath   = need("--in");
        else if (t == "--out")   a.outPath  = need("--out");
        else if (t == "--key")   a.keyPath  = need("--key");
        else if (t == "--iv")    a.ivPath   = need("--iv");
        else if (t == "--mask")  a.maskPath = need("--mask");
        else if (t == "--bits")  a.bitsSpec = need("--bits");
        else { printUsage(argv[0]); throw std::runtime_error("Unknown argument: " + t); }
    }
    if (a.op != "encrypt" && a.op != "decrypt") {
        printUsage(argv[0]); throw std::runtime_error("Invalid --op (encrypt|decrypt)");
    }
    if (a.mode != "full" && a.mode != "bits") {
        printUsage(argv[0]); throw std::runtime_error("Invalid --mode (full|bits)");
    }
    if (a.mode == "bits" && a.bitsSpec.empty()) {
        printUsage(argv[0]); throw std::runtime_error("mode=bits requires --bits");
    }
    if (a.inPath.empty() || a.outPath.empty() || a.keyPath.empty() || a.ivPath.empty()) {
        printUsage(argv[0]); throw std::runtime_error("Missing required arguments");
    }
    return a;
}

// --------- Crypto helpers ----------
struct KeyIv {
    cp::SecByteBlock key;
    std::vector<unsigned char> iv;
};

KeyIv loadOrCreateKeyIv(const Args& a, size_t /*dataSize*/) {
    KeyIv k{ cp::SecByteBlock(cp::AES::DEFAULT_KEYLENGTH), std::vector<unsigned char>(cp::AES::BLOCKSIZE) };
    if (a.op == "encrypt") {
        cp::AutoSeededRandomPool rng;
        rng.GenerateBlock(k.key, k.key.size());
        rng.GenerateBlock(k.iv.data(), k.iv.size());
        if (!writeFile(a.keyPath, k.key.data(), k.key.size()) ||
            !writeFile(a.ivPath,  k.iv.data(),  k.iv.size())) {
            throw std::runtime_error("Failed to write key/iv files.");
        }
    } else {
        // Reuse existing files
        if (!readFile(a.keyPath, reinterpret_cast<std::vector<unsigned char>&>(k.key), k.key.size()) ||
            !readFile(a.ivPath,  k.iv, k.iv.size())) {
            throw std::runtime_error("Failed to read key/iv files.");
        }
    }
    return k;
}

// Make a binary (0/255) mask matching image size. If no mask path, return full-255 mask.
cv::Mat makeEffectiveMask(const cv::Mat& img, const std::string& maskPath) {
    if (maskPath.empty()) {
        return cv::Mat(img.rows, img.cols, CV_8UC1, cv::Scalar(255));
    }
    cv::Mat m = cv::imread(maskPath, cv::IMREAD_GRAYSCALE);
    if (m.empty()) throw std::runtime_error("Failed to read mask image: " + maskPath);
    if (m.size() != img.size()) {
        cv::Mat resized; cv::resize(m, resized, img.size(), 0, 0, cv::INTER_NEAREST);
        m = resized;
    }
    cv::threshold(m, m, 128, 255, cv::THRESH_BINARY);
    return m;
}

// --------- FULL mode (byte-wise) using CTR keystream ----------
cv::Mat processFullCTR(const cv::Mat& img, const cv::Mat& mask, const KeyIv& kiv) {
    CV_Assert(img.type() == CV_8UC3);
    const size_t dataSize = img.total() * img.elemSize();
    std::vector<unsigned char> plain(img.data, img.data + dataSize);

    std::vector<unsigned char> zeroBuf(dataSize, 0), keystream(dataSize);
    cp::CTR_Mode<cp::AES>::Encryption ctr(kiv.key, kiv.key.size(), kiv.iv.data());
    ctr.ProcessData(keystream.data(), zeroBuf.data(), dataSize);

    std::vector<unsigned char> out(dataSize);
    const int channels = img.channels();
    for (size_t i = 0; i < dataSize; ++i) {
        size_t pixelIdx = i / channels;
        if (mask.data[pixelIdx])
            out[i] = plain[i] ^ keystream[i];
        else
            out[i] = plain[i];
    }

    cv::Mat result(img.rows, img.cols, img.type(), out.data());
    return result.clone();
}

// --------- Generic BIT-PLANE mode using CTR keystream ----------
cv::Mat processSelectedBitsCTR(const cv::Mat& img,
                               const cv::Mat& mask,
                               const KeyIv& kiv,
                               const std::vector<int>& bitPositions /* descending */) {
    CV_Assert(img.type() == CV_8UC3);
    const size_t totalBytes = img.total() * img.channels();
    const unsigned char* in = img.data;

    // Gather indices of bytes affected (by mask)
    std::vector<size_t> indices; indices.reserve(totalBytes);
    const int channels = img.channels();
    for (size_t i = 0; i < totalBytes; ++i) {
        size_t pixelIdx = i / channels;
        if (mask.data[pixelIdx]) indices.push_back(i);
    }

    const size_t N = indices.size();
    const size_t B = bitPositions.size();
    const size_t totalBits = N * B;
    const size_t packedSize = (totalBits + 7) / 8;

    // Pack: for each byte index → for each bitPosition (desc), append bit.
    std::vector<unsigned char> packed(std::max<size_t>(1, packedSize), 0);
    for (size_t k = 0; k < N; ++k) {
        const unsigned char v = in[indices[k]];
        size_t baseBit = k * B; // starting bit offset for this byte
        for (size_t b = 0; b < B; ++b) {
            int pos = bitPositions[b];
            unsigned char bit = (v >> pos) & 0x01;
            size_t bitOrdinal = baseBit + b;         // [0..totalBits-1]
            size_t byteIdx    = bitOrdinal / 8;
            int bitIdx        = 7 - static_cast<int>(bitOrdinal % 8);
            packed[byteIdx]  |= (bit << bitIdx);
        }
    }

    // Encrypt the packed bitstream with AES-CTR (XOR keystream)
    if (packedSize > 0) {
        std::vector<unsigned char> zeroBuf(packedSize, 0), ks(packedSize);
        cp::CTR_Mode<cp::AES>::Encryption ctr(kiv.key, kiv.key.size(), kiv.iv.data());
        ctr.ProcessData(ks.data(), zeroBuf.data(), packedSize);
        for (size_t j = 0; j < packedSize; ++j) packed[j] ^= ks[j];
    }

    // Unpack back into selected bit positions
    cv::Mat outImg = img.clone();
    unsigned char* out = outImg.data;
    for (size_t k = 0; k < N; ++k) {
        unsigned char v = out[indices[k]];
        size_t baseBit = k * B;
        for (size_t b = 0; b < B; ++b) {
            size_t bitOrdinal = baseBit + b;
            size_t byteIdx    = bitOrdinal / 8;
            int bitIdx        = 7 - static_cast<int>(bitOrdinal % 8);
            unsigned char newBit = (packed[byteIdx] >> bitIdx) & 0x01;

            int pos = bitPositions[b];
            v = static_cast<unsigned char>((v & ~(1u << pos)) | (newBit << pos));
        }
        out[indices[k]] = v;
    }
    return outImg;
}

int main(int argc, char* argv[]) {
    try {
        Args a = parseArgs(argc, argv);

        // Read and normalize image to 8UC3
        cv::Mat img = cv::imread(a.inPath, cv::IMREAD_COLOR);
        if (img.empty()) throw std::runtime_error("Failed to read input image: " + a.inPath);
        if (img.type() != CV_8UC3) {
            cv::Mat converted;
            if (img.channels() == 1) cv::cvtColor(img, converted, cv::COLOR_GRAY2BGR);
            else if (img.depth() != CV_8U) {
                img.convertTo(converted, CV_8U);
                if (converted.channels() != 3) cv::cvtColor(converted, converted, cv::COLOR_BGR2RGB);
            } else if (img.channels() == 4) {
                cv::cvtColor(img, converted, cv::COLOR_BGRA2BGR);
            } else {
                converted = img;
            }
            img = converted;
        }

        cv::Mat mask = makeEffectiveMask(img, a.maskPath);
        const size_t dataSize = img.total() * img.elemSize();
        KeyIv kiv = loadOrCreateKeyIv(a, dataSize);

        cv::Mat out;
        if (a.mode == "full") {
            out = processFullCTR(img, mask, kiv);
        } else {
            // Parse and validate bits list
            std::vector<int> bits = parseBitsList(a.bitsSpec);
            out = processSelectedBitsCTR(img, mask, kiv, bits);
        }

        if (!cv::imwrite(a.outPath, out)) {
            throw std::runtime_error("Failed to write output image: " + a.outPath);
        }

        std::cout << " " << (a.op == "encrypt" ? "Encrypted" : "Decrypted")
                  << " (" << a.mode
                  << (a.mode == "bits" ? (", bits=" + a.bitsSpec) : "")
                  << (a.maskPath.empty() ? ", no mask" : ", masked")
                  << ") \u2192 " << a.outPath << "\n"
                  << "Key: " << a.keyPath << "   IV: " << a.ivPath << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
