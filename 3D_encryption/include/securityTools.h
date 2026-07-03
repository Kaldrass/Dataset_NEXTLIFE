#ifndef SECURITY_TOOLS_H
#define SECURITY_TOOLS_H

// CPP
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>                              // std::cout (debug)
#include <string>                                // std::string
#include <random>                                // std::seed_seq / std::mt19937 / std::uniform_int_distribution
#include <boost/math/special_functions/beta.hpp> // for ibeta (deformations monotonic)

// Crypto++
#include <cryptopp/aes.h>      // CryptoPP::AES
#include <cryptopp/modes.h>    // CryptoPP::CFB_Mode<CryptoPP::AES>
#include <cryptopp/osrng.h>    // CryptoPP::AutoSeededRandomPool
#include <cryptopp/hex.h>      // CryptoPP::HexDecoder
#include <cryptopp/hkdf.h>     // CryptoPP::HKDF for Lorenz encryption
#include <cryptopp/sha.h>      // CryptoPP::HKDF for Lorenz encryption + Hierarchical
#include <cryptopp/cryptlib.h> // SimpleKeyingInterface (probe)
// ME
#include "handleJSON.h"

////////////////////////////////////////////////////////////////
////////                  ChaCha20 PRNG                  ///////
////////////////////////////////////////////////////////////////

class SimpleCryptoPRNG
{
private:
    CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption ctr_;
    std::array<CryptoPP::byte, 64> buffer_;
    size_t offset_;

    void refill_()
    {
        std::array<CryptoPP::byte, 64> zeros{};
        ctr_.ProcessData(buffer_.data(), zeros.data(), buffer_.size());
        offset_ = 0;
    }

    // Helper for KS p-value via the infinite series Q(z)
    static double ksQ(double z)
    {
        const double TOL = 1e-12;
        double sum = 0.0;
        for (int k = 1;; ++k)
        {
            double term = std::exp(-2.0 * k * k * z * z);
            if (term < TOL)
                break;
            sum += (k % 2 ? +term : -term);
        }
        return 2.0 * sum;
    }

public:
    static constexpr uint32_t FULL_RANGE = 0xFFFFFFFFu;

    explicit SimpleCryptoPRNG(const CryptoPP::SecByteBlock &key)
    {
        CryptoPP::SecByteBlock iv(CryptoPP::AES::BLOCKSIZE);
        std::memset(iv, 0, iv.size());
        ctr_.SetKeyWithIV(key, key.size(), iv, iv.size());
        offset_ = buffer_.size();
    }

    // raw 32-bit
    uint32_t operator()()
    {
        if (offset_ + sizeof(uint32_t) > buffer_.size())
            refill_();
        uint32_t v;
        std::memcpy(&v, buffer_.data() + offset_, sizeof(v));
        offset_ += sizeof(v);
        return v;
    }

    bool nextBool() { return ((*this)() & 1u) != 0; }

    float nextFloat(float a, float b)
    {
        uint32_t r = (*this)();
        float u = float(r) / float(FULL_RANGE);
        return a + u * (b - a);
    }

    uint32_t nextUInt(uint32_t a, uint32_t b)
    {
        uint32_t range = b - a;
        if (range == 0)
            return a;
        uint32_t r = (*this)();
        return a + (r % range);
    }

    int nextInt(int a, int b)
    {
        return static_cast<int>(nextUInt(uint32_t(a), uint32_t(b)));
    }

    // ——— 1) Reproducibility check —————————————————————
    // Returns true iff two copies (passed by value) produce identical
    // raw, bool, and float(a,b) for `trials` steps.
    static bool verifyReproducibility(SimpleCryptoPRNG prngA, SimpleCryptoPRNG prngB, size_t trials, float a, float b)
    {
        for (size_t i = 0; i < trials; ++i)
        {
            // 1) raw 32-bit
            uint32_t r1 = prngA();
            uint32_t r2 = prngB();
            if (r1 != r2)
                return false;

            // 2) bool
            bool bb1 = prngA.nextBool();
            bool bb2 = prngB.nextBool();
            if (bb1 != bb2)
                return false;

            // 3) float
            float f1 = prngA.nextFloat(a, b);
            float f2 = prngB.nextFloat(a, b);
            if (std::memcmp(&f1, &f2, sizeof(float)) != 0)
                return false;
        }
        return true;
    }

    // ——— 2) Basic statistical tests —————————————————————
    // Runs Monobit, Runs, and KS on `trials` samples.
    // Outputs p-values by reference.
    static void testRandomness(SimpleCryptoPRNG prng, size_t trials, float a, float b, double &p_monobit, double &p_runs, double &p_ks)
    {
        // --- Monobit ---
        size_t ones = 0;
        for (size_t i = 0; i < trials; ++i)
            if (prng.nextBool())
                ++ones;
        double n = double(trials);
        double s_obs = std::fabs(double(ones) - (n - ones)) / std::sqrt(n);
        p_monobit = std::erfc(s_obs / std::sqrt(2.0));

        // --- Runs ---
        // We need the bits again, so reset prng:
        prng = SimpleCryptoPRNG(prng); // relies on copy ctor to rewind
        std::vector<int> bits(trials);
        for (size_t i = 0; i < trials; ++i)
            bits[i] = prng.nextBool();
        double pi = double(ones) / n;
        // if frequency too far from 0.5, runs test is invalid:
        if (std::fabs(pi - 0.5) > (2.0 / std::sqrt(n)))
        {
            p_runs = 0.0;
        }
        else
        {
            size_t runs = 1;
            for (size_t i = 1; i < trials; ++i)
                if (bits[i] != bits[i - 1])
                    ++runs;
            double numerator = std::fabs(double(runs) - 2.0 * n * pi * (1 - pi));
            double denominator = 2.0 * std::sqrt(2.0 * n) * pi * (1 - pi);
            p_runs = std::erfc(numerator / denominator);
        }

        // --- KS on floats [a,b) ---
        prng = SimpleCryptoPRNG(prng); // rewind again
        std::vector<double> vals(trials);
        for (size_t i = 0; i < trials; ++i)
        {
            float f = prng.nextFloat(a, b);
            // map back to [0,1)
            vals[i] = (f - a) / (b - a);
        }
        std::sort(vals.begin(), vals.end());
        double d_plus = 0.0, d_minus = 0.0;
        for (size_t i = 0; i < trials; ++i)
        {
            double Fi = double(i + 1) / n;
            d_plus = std::max(d_plus, Fi - vals[i]);
            d_minus = std::max(d_minus, vals[i] - double(i) / n);
        }
        double D = std::max(d_plus, d_minus);
        p_ks = ksQ(D * std::sqrt(n));
        if (p_ks > 1.0)
            p_ks = 1.0;
    }
};

////////////////////////////////////////////////////////////////
////////              LORENZ 3D ENCRYPTION               ///////
////////////////////////////////////////////////////////////////

// Map uint64 → [0,1)
inline double u01_from_u64(uint64_t x)
{
    const uint64_t mant = (x >> 11) | 1ULL;           // 53-bit mantissa
    return double(mant) * (1.0 / 9007199254740992.0); // 2^-53
}
inline double lerp(double u, double a, double b) { return a + (b - a) * u; }

std::array<double, 3> lorenz_deriv(const std::array<double, 3> &s, double sigma = 10.0, double rho = 28.0, double beta = 8.0 / 3.0);

void lorenz_rk4_step(std::array<double, 3> &s, double dt);

// Derive (x0,y0,z0) from key+iv using HKDF-SHA256
void derive_lorenz_initials_from_key(const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, double &x0, double &y0, double &z0);

// Generate per-vertex keys as floats (Kx,Ky,Kz)
void generate_lorenz_keys_3f(std::vector<float> &K, size_t Nverts, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv);

// Standalone: encrypt/decrypt in-place on flat [x,y,z]*
void computeSolo_LorenzAffine(float *verts, size_t count, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, bool isEncryption);

////////////////////////////////////////////////////////////////
////////                      KEYS                       ///////
////////////////////////////////////////////////////////////////

/**
 * @brief Encode a CryptoPP::SecByteBlock to a hexadecimal string.
 *
 * This function encodes the contents of a `CryptoPP::SecByteBlock` into a string
 * where each byte is represented by a pair of hexadecimal characters.
 *
 * @param block The `CryptoPP::SecByteBlock` to encode.
 * @return A string containing the hexadecimal representation of the block.
 */
std::string encodeHexStringSecByteBlock(const CryptoPP::SecByteBlock &block);

/**
 * @brief Decodes a hexadecimal string back into a CryptoPP::SecByteBlock.
 *
 * This function decodes a string where each pair of hexadecimal characters
 * represents one byte, converting it back into a `CryptoPP::SecByteBlock`.
 *
 * @param blockString A string containing the hexadecimal representation of a byte block.
 * @return A `CryptoPP::SecByteBlock` containing the decoded bytes.
 */
CryptoPP::SecByteBlock decodeHexStringSecByteBlock(const std::string &blockString);

////////////////////////////////////////////////////////////////
////////                      CORE                       ///////
////////////////////////////////////////////////////////////////

/**
 * @brief Validates bit-field parameters for packing into an integer type.
 *
 * Ensures that `nbBitsTarget` is non-zero, `nbBitsOffset` is non-negative and
 * that `nbBitsTarget + nbBitsOffset` does not exceed the bit capacity of the
 * destination type (`sizeByteType * 8`).
 *
 * @param nbBitsTarget  Number of bits allocated to the field (must be > 0).
 * @param nbBitsOffset  Bit offset on LSB (must be >= 0).
 * @param sizeByteType  Size in bytes of the destination integer type (must be > 0).
 *
 * @note Terminates the program with `EXIT_FAILURE` if any check fails.
 */
void checkOverflowParams(size_t nbBitsTarget, size_t nbBitsOffset, size_t sizeByteType);

/**
 * @brief Lightweight bit-level cursor operating LSB-first within a byte buffer.
 *
 * This helper struct tracks a logical bit position as a (byte, bit) pair, where
 * bits are indexed from 0 (LSB) to 7 (MSB) inside each byte. It provides
 * convenience methods for reading, writing, and XOR-ing a single bit at the
 * current position, and for advancing to the next bit.
 *
 * The cursor is intended to be used on raw byte buffers (e.g. reinterpret_cast
 * from structured types) to implement bit-precise extraction or insertion without
 * repeatedly computing byte/bit indices.
 *
 * Usage example:
 * @code
 * BitCursor c{0, 0};           // start at bit 0 of byte 0
 * uint8_t bit = c.get(buf);    // read bit
 * c.set(buf, 1);               // set bit to 1
 * c.xorb(buf, bit);            // XOR current bit with 'bit'
 * c.advance();                 // move to next bit (LSB→MSB, then next byte)
 * @endcode
 */
struct BitCursor
{
    size_t byte; // byte index
    size_t bit;  // bit index [0..7], LSB=0

    inline uint8_t get(const uint8_t *buf) const
    {
        return static_cast<uint8_t>((buf[byte] >> bit) & 0x1u);
    }
    inline void set(uint8_t *buf, uint8_t b) const
    {
        const uint8_t mask = static_cast<uint8_t>(1u << bit);
        buf[byte] = static_cast<uint8_t>((buf[byte] & ~mask) | ((b & 1u) << bit));
    }
    inline void xorb(uint8_t *buf, uint8_t b) const
    {
        buf[byte] = static_cast<uint8_t>(buf[byte] ^ ((b & 1u) << bit));
    }
    inline void advance()
    {
        ++bit;
        byte += (bit >> 3);
        bit &= 7;
    }
};

/**
 * @brief Extracts or inserts a contiguous bit window for all elements into/from a linear byte stream.
 *
 * This function walks, for each element in @p data, the bit range
 * [@p startIndex, @p endIndex) (in element-local bit indices) and maps it
 * to/from a flat bit-addressed byte stream, using LSB-first order inside each
 * byte on both sides.
 *
 * Conceptually, the byte stream is treated as a single infinite bit array,
 * indexed from 0 upward. For each element:
 *  - If @p isExtract is true, bits are copied from @p data into @p byteStream.
 *  - If @p isExtract is false, bits are copied from @p byteStream into @p data.
 *
 * @tparam DataType
 *   Trivial byte-addressable type of the elements in @p data (e.g., uint32_t,
 *   float, double). Elements are treated purely as raw bytes; numeric
 *   endianness does not matter.
 *
 * @param data
 *   Pointer to the array of elements whose bits are to be processed in-place.
 * @param dataSize
 *   Number of elements in @p data.
 * @param byteStream
 *   Pointer to the byte stream that serves as a global bit buffer across all
 *   elements. Bits are filled/consumed sequentially from bit index 0 upward.
 * @param startIndex
 *   First bit index (inclusive) within each element to process, counted from
 *   the element's LSB, with LSB-first ordering inside each byte.
 * @param endIndex
 *   One-past-last bit index (exclusive) within each element to process.
 *   Must satisfy endIndex > startIndex and endIndex ≤ 8 * sizeof(DataType).
 * @param isExtract
 *   Operation mode:
 *     - true  : extract bits from @p data into @p byteStream, preserving bit order.
 *     - false : insert bits from @p byteStream back into @p data at the same positions.
 *
 * @note The caller is responsible for ensuring that @p byteStream has enough
 *       capacity for dataSize * (endIndex - startIndex) bits, i.e.
 *       ceil(dataSize * (endIndex - startIndex) / 8) bytes.
 */
template <typename DataType>
void processFull_Bit(DataType *data, size_t dataSize, uint8_t *byteStream, size_t startIndex, size_t endIndex, bool isExtract)
{
    if (!data || !byteStream || dataSize == 0 || startIndex >= endIndex)
        return;

    const size_t bitsPerElem = endIndex - startIndex;
    const size_t startByte = startIndex / 8;
    const size_t startBit = startIndex % 8;

    BitCursor bs{0, 0}; // Cursor over the byteStream (global across all elements)

    for (size_t i = 0; i < dataSize; ++i)
    {
        auto *cur = reinterpret_cast<uint8_t *>(&data[i]);
        BitCursor d{startByte, startBit}; // Cursor over the targeted bits of this element

        for (size_t b = 0; b < bitsPerElem; ++b)
        {
            if (isExtract)
                bs.set(byteStream, d.get(cur)); // byteStream_bit := data_bit
            else
                d.set(cur, bs.get(byteStream)); // data_bit := byteStream_bit

            d.advance();
            bs.advance();
        }
    }
}

////////////////////////////////////////////////////////////////
////////                ENCRYPTION PROCESS               ///////
////////////////////////////////////////////////////////////////

/**
 * @brief Selectively encrypts/decrypts bit ranges of an array using a generic AES mode.
 *
 * This routine implements a two-step "extract–transform–insert" pipeline:
 *   1. A contiguous bit window of each element in @p data
 *      (nbBitsTarget bits starting at nbBitsOffset) is extracted, element by element,
 *      into a flat byte stream using @ref processFull_Bit.
 *   2. The resulting byte stream is transformed in-place by a Crypto++ AES mode
 *      (e.g. CFB, CTR, OFB, CBC, ECB) and then written back into the same bit
 *      positions in @p data.
 *
 * Stream-like modes (CTR/CFB/OFB) are treated as arbitrary-length keystream
 * ciphers and can operate on any total bit length. Block modes (CBC/ECB) are
 * treated as true block ciphers: the total number of protected bits must be an
 * exact multiple of the AES block size (128 bits), and no padding is performed
 * beyond that. This ensures that every AES block is fully recoverable on
 * decryption without storing extra ciphertext bits.
 *
 * @tparam Mode
 *   Crypto++ mode type providing a symmetric cipher interface, e.g.
 *   - CryptoPP::ECB_Mode<CryptoPP::AES>::Encryption / ::Decryption
 *   - CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption / ::Decryption
 *   - CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption / ::Decryption
 *   - CryptoPP::OFB_Mode<CryptoPP::AES>::Encryption / ::Decryption
 *   - CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption / ::Decryption
 *
 * @tparam DataType
 *   Trivial, byte-addressable element type in @p data (e.g., int, uint32_t, float,
 *   double). Elements are treated as raw bytes.
 *
 * @param data
 *   Pointer to the array of elements whose bits are to be transformed in-place.
 * @param dataSize
 *   Number of elements in @p data.
 * @param key
 *   AES key as a CryptoPP::SecByteBlock. Must satisfy Mode::IsValidKeyLength().
 * @param iv
 *   Initialization vector as a CryptoPP::SecByteBlock. Required and checked
 *   only if the selected @p Mode reports an IV requirement (e.g. CBC/CFB/CTR/OFB).
 *   Modes like ECB ignore the IV and use SetKey() without IV.
 * @param nbBitsTarget
 *   Number of bits per element to protect. This bit window starts at bit
 *   offset @p nbBitsOffset inside each element.
 * @param nbBitsOffset
 *   Bit offset (from the element's LSB, with LSB-first order inside each byte)
 *   of the first bit to process.
 *
 * @pre
 *   - checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType)) must hold:
 *       nbBitsOffset + nbBitsTarget ≤ 8 * sizeof(DataType).
 *   - @p key length is valid for @p Mode (aes.IsValidKeyLength(key.size()) == true).
 *   - If Mode is IV-based (aes.IsResynchronizable() == true), then iv.size() == aes.IVSize().
 *   - Let totalBits = dataSize * nbBitsTarget:
 *       * If aes.MandatoryBlockSize() == 1 (CTR/CFB/OFB), totalBits may be arbitrary.
 *       * If aes.MandatoryBlockSize() > 1 (CBC/ECB), totalBits must be a multiple
 *         of (aes.MandatoryBlockSize() * 8). No padding is added.
 *
 * @post
 *   - On encryption: the selected bits are replaced by their ciphertext image
 *     under the chosen AES mode; non-selected bits remain unchanged.
 *   - On decryption (with identical Mode, key, iv, nbBitsTarget, nbBitsOffset),
 *     the original bits in the selected window are fully restored.
 *
 * @note
 *   This function is designed for selective bit-level protection over arbitrary
 *   structured data (e.g., 3D coordinates). It relies on @ref processFull_Bit
 *   to provide a stable mapping between per-element bit windows and a linear
 *   byte stream. For block modes (CBC/ECB), the full-block alignment requirement
 *   is critical to ensure reversibility without storing extra ciphertext state.
 */
template <typename Mode, typename DataType>
void computeFull_AES(DataType *const data, const size_t &dataSize, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, const size_t &nbBitsTarget, const size_t &nbBitsOffset)
{
    // Sanity check
    checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType));
    Mode aes;

    // Check Key
    if (!aes.IsValidKeyLength(key.size()))
    {
        std::cerr << "[AES] - ERROR: Invalid key length (" << key.size() << ") for this mode/cipher.\n";
        std::exit(EXIT_FAILURE);
    }

    // --- Check IV requirement & size ---
    CryptoPP::SimpleKeyingInterface::IV_Requirement ivReq = aes.IVRequirement();
    const bool usesIV = ivReq < CryptoPP::SimpleKeyingInterface::NOT_RESYNCHRONIZABLE;
    const size_t ivExpected = aes.IVSize(); // usually block size (16 for AES)
    if (usesIV)
    {
        // CBC/CFB/CTR/OFB/...: IV is required
        if (iv.size() != ivExpected)
        {
            std::cerr << "[AES] - ERROR: Invalid IV. Expected " << ivExpected << " bytes, got " << iv.size() << ".\n";
            std::exit(EXIT_FAILURE);
        }
    }

    // --- Bit and byte counts ---
    const size_t totalBits = dataSize * nbBitsTarget;
    const size_t mbs = aes.MandatoryBlockSize(); // MandatoryBlockSize: 1 for CTR/CFB/OFB; 16 for CBC/ECB (AES)
    size_t needBytes = 0;

    if (mbs > 1)
    {
        // Block cipher mode (CBC/ECB): enforce full-block alignment
        const size_t blockBits = mbs * 8; // 128 for AES

        if (totalBits % blockBits != 0)
        {
            std::cerr << "[AES] - ERROR: Block mode (block size "
                      << blockBits << " bits) requires totalBits="
                      << totalBits << " to be a multiple of block size.\n"
                      << "         Either adjust nbBitsTarget*dataSize or use a stream mode.\n";
            std::exit(EXIT_FAILURE);
        }
        needBytes = totalBits / 8; // Exactly full blocks, no padding
    }
    else
    {
        needBytes = (totalBits + 7) / 8; // Stream-like modes (CTR/CFB/OFB): arbitrary bit lengths, no padding needed
    }

    // Zero-initialized buffer, size 'need' {Padding if needed}
    std::vector<uint8_t> byteStream(needBytes, 0);

    // Extract actual bits into the first rawBytes bytes (rest stays zero) : data(MSB<-LSB) --> byteStream(MSB<-LSB)
    processFull_Bit(data, dataSize, byteStream.data(), nbBitsOffset, nbBitsOffset + nbBitsTarget, /*isExtract=*/true);

    // AES : byteStream --> byteStream
    if (usesIV)
        aes.SetKeyWithIV(key, key.size(), iv.data(), iv.size());
    else
        aes.SetKey(key, key.size()); // ECB/CBC
    aes.ProcessData(byteStream.data(), byteStream.data(), needBytes);

    // Insert back only the original bits (ignore padded tail) : byteStream(MSB<-LSB) --> data(MSB<-LSB)
    processFull_Bit(data, dataSize, byteStream.data(), nbBitsOffset, nbBitsOffset + nbBitsTarget, /*isExtract=*/false);
}

/**
 * @brief Performs PRNG XOR selective encryption/decryption over a flattened bitstream window.
 *
 * This function:
 *   1. Extracts nbBitsTarget bits per element (starting at nbBitsOffset) from @p data
 *      into a contiguous byte stream via processFull_Bit().
 *   2. XORs that byte stream with a PRNG stream derived from @p key.
 *   3. Writes the modified bits back into the same positions in @p data.
 *
 * The operation is symmetric: calling it twice with the same parameters restores
 * the original bits.
 *
 * @tparam DataType
 *   Trivial, byte-addressable element type in @p data (e.g., int, uint32_t, float, double).
 *
 * @param data
 *   Pointer to the array of elements to process in-place.
 * @param dataSize
 *   Number of elements in @p data.
 * @param key
 *   PRNG seed material as a CryptoPP::SecByteBlock.
 * @param nbBitsTarget
 *   Number of bits per element to protect (bit window size).
 * @param nbBitsOffset
 *   Bit offset (from the element's LSB, LSB-first inside each byte) of the first bit
 *   to include in the window.
 */
template <typename DataType>
void computeFull_XOR(DataType *const data, const size_t &dataSize, const CryptoPP::SecByteBlock &key, const size_t &nbBitsTarget, const size_t &nbBitsOffset)
{
    // Sanity check on bit window (same as other helpers)
    checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType));

    // Total number of protected bits (flattened over all elements)
    const size_t totalBits = dataSize * nbBitsTarget;
    const size_t byteStreamSize = (totalBits + 7) / 8; // ceil(totalBits / 8)

    // Zero-initialized byte stream
    std::vector<uint8_t> byteStream(byteStreamSize, 0);

    // --- Extract: data -> byteStream ---
    processFull_Bit(data, dataSize, byteStream.data(), nbBitsOffset, nbBitsOffset + nbBitsTarget, /*isExtract=*/true);

    // --- PRNG XOR over the full stream ---
    std::seed_seq seed(key.begin(), key.end());
    std::mt19937 generator(seed);
    std::uniform_int_distribution<uint32_t> distribution(0u, 0xFFu);
    for (size_t i = 0; i < byteStreamSize; ++i)
    {
        const uint8_t rbyte = static_cast<uint8_t>(distribution(generator));
        byteStream[i] ^= rbyte;
    }

    // --- Insert back: byteStream -> data ---
    processFull_Bit(data, dataSize, byteStream.data(), nbBitsOffset, nbBitsOffset + nbBitsTarget, /*isExtract=*/false);
}

/**
 * @brief Selectively encrypts/decrypts bit ranges of array elements using a custom AES–CFB-m variant.
 *
 * This routine applies a per-element, bit-granular CFB-style transformation over a contiguous bit window
 * within each element of `data`. Bits are traversed LSB→MSB within each byte. The construction uses a single
 * 128-bit feedback register R (initialized with IV), where the keystream is read directly from R and the
 * feedback is written back into R at the same bit positions:
 *
 *  - Before processing the first element: R ← E_K(IV).
 *  - For each element:
 *      * For b = 0..(nbBitsTarget-1) (walking the target window LSB→MSB):
 *          - s ← bit_b(R)            // keystream bit read from R
 *          - On encryption:  c = p ⊕ s; write back feedback as R_bit ^= p; data_bit ← c.
 *          - On decryption:  p = c ⊕ s; write back feedback as R_bit  = c; data_bit ← p.
 *        (In both directions the updated bit of R becomes the ciphertext bit c.)
 *      * After the element: R ← E_K(R).
 *
 * This matches the behavior of your previous implementation (same-position feedback; no shift-register
 * movement between bits; re-encrypt R once per element).
 *
 * @tparam DataType
 *   Trivial byte-addressable element type in `data` (e.g., uint32_t, float, double...). The function treats
 *   elements as raw bytes; endianness of numeric interpretation is irrelevant.
 *
 * @param data
 *   Pointer to the element array to process in-place.
 * @param dataSize
 *   Number of elements in `data`.
 * @param key
 *   AES key (16/24/32 bytes). Must satisfy Crypto++ AES key length constraints.
 * @param iv
 *   Initialization vector (exactly 16 bytes). Used to seed the 128-bit register R.
 * @param nbBitsTarget
 *   Number of bits to process per element (must be ≤ 128).
 * @param nbBitsOffset
 *   Bit offset (from the element's LSB, LSB→MSB within bytes) of the first processed bit.
 * @param isEncryption
 *   If true, perform encryption (feedback R_bit ^= plaintext_bit). If false, perform decryption (feedback R_bit = ciphertext_bit).
 *
 * @pre
 *   - key.size() ∈ {16, 24, 32}
 *   - iv.size() == 16
 *   - nbBitsTarget ≤ 128
 *   - nbBitsOffset + nbBitsTarget ≤ 8 * sizeof(DataType)
 *   - data != nullptr
 *
 * @post
 *   - On encryption: targeted bits become ciphertext; non-targeted bits are unchanged.
 *   - On decryption: targeted bits are restored to the original plaintext; non-targeted bits are unchanged.
 *
 * @warning
 *   This is a custom CFB-m construction (same-position bit feedback, no intra-segment shifting).
 *
 * @remarks
 *   Complexity is O(dataSize * nbBitsTarget). For byte-aligned windows (nbBitsOffset % 8 == 0 and nbBitsTarget % 8 == 0),
 *   a fast path could process whole bytes.
 */
template <typename DataType>
void computeSolo_AES_CFB(DataType *const data, const size_t dataSize, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, const size_t nbBitsTarget, const size_t nbBitsOffset, const bool isEncryption)
{
    // Check overflow params
    checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType));

    // Check block cipher primitive
    CryptoPP::AES::Encryption aes;
    if (!aes.IsValidKeyLength(key.size()))
    {
        std::cerr << "[AES] - Invalid key length: " << key.size() << " (expected 16, 24, or 32)\n";
        std::exit(EXIT_FAILURE);
    }
    if (iv.size() != CryptoPP::AES::BLOCKSIZE)
    {
        std::cerr << "[AES] - Invalid IV length: " << iv.size() << " (expected " << CryptoPP::AES::BLOCKSIZE << " bytes)\n";
        std::exit(EXIT_FAILURE);
    }
    if (nbBitsTarget > CryptoPP::AES::BLOCKSIZE * 8)
    {
        std::cerr << "[AES/CFB-m] - nbBitsTarget (" << nbBitsTarget << ") exceeds AES block size (" << CryptoPP::AES::BLOCKSIZE * 8 << "); split or reduce.\n";
        std::exit(EXIT_FAILURE);
    }

    // Init block cipher primitive with secret key
    aes.SetKey(key, key.size());

    // Fixed indices
    const size_t startByte = nbBitsOffset / 8;
    const size_t startBit = nbBitsOffset % 8;

    // Single register R: read keystream bits FROM R and feed back INTO R (same positions)
    CryptoPP::SecByteBlock R(iv);
    aes.ProcessBlock(R.data(), R.data()); // R = E_K(R) with R=IV initially

    for (size_t i = 0; i < dataSize; ++i)
    {
        auto *cur = reinterpret_cast<uint8_t *>(&data[i]);

        BitCursor d{startByte, startBit}; // targeted region in data
        BitCursor ks{0, 0};               // bit position inside R (LSB→MSB within bytes)

        for (size_t b = 0; b < nbBitsTarget; ++b)
        {
            const uint8_t s = ks.get(R.data()); // keystream bit from R
            const uint8_t bit_in = d.get(cur);  // enc: plaintext p, dec: ciphertext c (before xor)

            if (isEncryption)
            {
                // FEEDBACK (encrypt): XOR R with plaintext bit -> bit becomes s ^ p == c
                ks.xorb(R.data(), bit_in);

                // OUTPUT: data ^= s (p -> c)
                d.xorb(cur, s);
            }
            else
            {
                // FEEDBACK (decrypt): SET R to ciphertext bit (bit_in is c here)
                ks.set(R.data(), bit_in);

                // OUTPUT: data ^= s (c -> p)
                d.xorb(cur, s);
            }

            d.advance();
            ks.advance();
        }

        aes.ProcessBlock(R.data(), R.data()); // R = E_K(R) for next element
    }
}

/**
 * @brief Performs PRNG XOR selective encryption or decryption per element on an array of DataType.
 *
 * This version uses BitCursor to traverse bits LSB→MSB inside each byte.
 *
 * @tparam DataType The type of the elements in the `data` array.
 *
 * @param data A pointer to the array of data elements to process.
 * @param dataSize The number of elements in the `data` array.
 * @param key The key used to seed the pseudo-random number generator, provided as a `CryptoPP::SecByteBlock`.
 * @param nbBitsTarget The number of bits to target for encryption in each data element.
 * @param nbBitsOffset The bit offset to start processing in each data element (LSB-first across bytes).
 */
template <typename DataType>
void computeSolo_XOR(DataType *const data, const size_t &dataSize, const CryptoPP::SecByteBlock &key, const size_t &nbBitsTarget, const size_t &nbBitsOffset)
{
    // Check overflow params
    checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType));

    // Starting position of the targeted window
    const size_t startByte = nbBitsOffset / 8;
    const size_t startBit = nbBitsOffset % 8;

    // Init PRNG XOR (same behavior as before: one generator for the whole array)
    std::seed_seq seed(key.begin(), key.end());
    std::mt19937 generator(seed);
    std::bernoulli_distribution distribution(0.5);

    // Iterate over each element in data
    for (size_t i = 0; i < dataSize; ++i)
    {
        // Treat the current element as raw bytes
        auto *cur = reinterpret_cast<uint8_t *>(&data[i]);

        // Cursor on the targeted region within this element
        BitCursor c{startByte, startBit};

        // Loop over the targeted bits
        for (size_t b = 0; b < nbBitsTarget; ++b)
        {
            const uint8_t rbit = static_cast<uint8_t>(distribution(generator)); // 0 or 1
            c.xorb(cur, rbit);                                                  // XOR current bit with PRNG bit (no change if rbit == 0)
            c.advance();                                                        // Move to next bit
        }
    }
}

////////////////////////////////////////////////////////////////
////////                      ATTACK                     ///////
////////////////////////////////////////////////////////////////

/**
 * @brief Performs a zero-ing attack on selected bits (set to 0) of each element in an array of DataType.
 *
 * This version uses BitCursor to traverse bits LSB→MSB inside each byte.
 *
 * @tparam DataType The type of the elements in the `data` array.
 *
 * @param data A pointer to the array of data elements to process.
 * @param dataSize The number of elements in the `data` array.
 * @param nbBitsTarget The number of bits to target for zeroing in each data element.
 * @param nbBitsOffset The bit offset to start processing in each data element (LSB-first across bytes).
 */
template <typename DataType>
void zeroed(DataType *const data, const size_t &dataSize, const size_t &nbBitsTarget, const size_t &nbBitsOffset)
{
    // Same safety check as before
    checkOverflowParams(nbBitsTarget, nbBitsOffset, sizeof(DataType));

    // Starting position of the targeted window
    const size_t startByte = nbBitsOffset / 8;
    const size_t startBit = nbBitsOffset % 8;

    for (size_t i = 0; i < dataSize; ++i)
    {
        // Treat the element as raw bytes
        auto *cur = reinterpret_cast<uint8_t *>(&data[i]);

        // Cursor on the targeted region within this element
        BitCursor c{startByte, startBit};

        // Zero nbBitsTarget bits, LSB→MSB across bytes
        for (size_t b = 0; b < nbBitsTarget; ++b)
        {
            c.set(cur, 0u); // clear current bit
            c.advance();    // move to next bit
        }
    }
}

////////////////////////////////////////////////////////////////
////////             HIERARCHICAL ENCRYPTION             ///////
////////////////////////////////////////////////////////////////

/**
 * @brief Extracts or reinserts three MSB-side bit fields (P, Q, R) per element
 *        into/from separate byte streams.
 *
 * For each element of type DataType (B = 8 * sizeof(DataType) bits):
 *
 *   MSB                                    LSB
 *    | P...P | Q...Q | R...R |   X...X   |
 *      ^p bits  ^q bits  ^r bits  ^rest
 *
 * Where:
 *   - P: highest p bits of the element
 *   - Q: next q bits
 *   - R: next r bits
 *   - X: untouched remainder (B - (p + q + r) bits)
 *
 * Bit numbering is LSB-first for implementation:
 *   - element bit index j ∈ [0, B-1], j=0 is LSB, j=B-1 is MSB.
 *   - P covers indices [B-p, ..., B-1]
 *   - Q covers [B-p-q, ..., B-p-1]
 *   - R covers [B-p-q-r, ..., B-p-q-1]
 *
 * All streams are packed LSB-first within each byte, element by element,
 * left-to-right in the order: for each element, all P bits, then Q bits, then R bits.
 *
 * @tparam DataType
 *   Trivial byte-addressable type (e.g., int32_t, uint32_t, float, double).
 *
 * @param data
 *   Pointer to the element array (processed in-place).
 * @param dataSize
 *   Number of elements in @p data.
 * @param streamP
 *   Byte buffer holding P bits (size ≥ ceil(dataSize * p / 8)).
 * @param streamQ
 *   Byte buffer holding Q bits (size ≥ ceil(dataSize * q / 8)).
 * @param streamR
 *   Byte buffer holding R bits (size ≥ ceil(dataSize * r / 8)).
 * @param p
 *   Number of MSB bits per element in the P field.
 * @param q
 *   Number of bits per element in the Q field (just below P).
 * @param r
 *   Number of bits per element in the R field (just below Q).
 * @param isExtract
 *   - true:  data → streams  (read bits from data, write into streams)
 *   - false: streams → data  (read bits from streams, write into data)
 */
template <typename DataType>
void processMSB_PQR(DataType *const data, const size_t dataSize, uint8_t *const streamP, uint8_t *const streamQ, uint8_t *const streamR, const size_t p, const size_t q, const size_t r,
                    const bool isExtract, const bool pEnabled, const bool qEnabled, const bool rEnabled)
{
    const size_t B_eff = 23; // bits for mantissa of floats only!

    // Sanity check: P + Q + R must fit in the element
    if (p + q + r > B_eff)
    {
        std::cerr << "[processMSB_PQR] - ERROR: p+q+r (" << (p + q + r) << ") exceeds element bit width (" << B_eff << ").\n";
        std::exit(EXIT_FAILURE);
    }

    // Effective band: [base .. base + B_eff - 1]; for float, base = 0, band = [0..22].
    const size_t base = 0;
    const size_t windowLen = p + q + r;

    // Place P/Q/R at the *top* of that band:
    const size_t x = base + (B_eff - windowLen); // first bit index of the R field
    const size_t startR = x;                     // R: [x .. x + r - 1]
    const size_t startQ = x + r;                 // Q: [x + r .. x + r + q - 1]
    const size_t startP = x + r + q;             // P: [x + r + q .. x + r + q + p - 1]

    // Stream cursors (global over the whole array)
    BitCursor cP{0, 0};
    BitCursor cQ{0, 0};
    BitCursor cR{0, 0};

    for (size_t i = 0; i < dataSize; ++i)
    {
        auto *cur = reinterpret_cast<uint8_t *>(&data[i]);

        // --- Field P (highest p bits) ---
        if (p > 0 && pEnabled)
        {
            BitCursor dP{startP / 8, startP % 8}; // per-element data cursor
            for (size_t b = 0; b < p; ++b)
            {
                if (isExtract)
                {
                    const uint8_t bit = dP.get(cur); // read from data
                    cP.set(streamP, bit);            // write to stream
                }
                else
                {
                    const uint8_t bit = cP.get(streamP); // read from stream
                    dP.set(cur, bit);                    // write into data
                }
                dP.advance();
                cP.advance();
            }
        }

        // --- Field Q (next q bits) ---
        if (q > 0 && qEnabled)
        {
            BitCursor dQ{startQ / 8, startQ % 8};
            for (size_t b = 0; b < q; ++b)
            {
                if (isExtract)
                {
                    const uint8_t bit = dQ.get(cur);
                    cQ.set(streamQ, bit);
                }
                else
                {
                    const uint8_t bit = cQ.get(streamQ);
                    dQ.set(cur, bit);
                }
                dQ.advance();
                cQ.advance();
            }
        }

        // --- Field R (next r bits) ---
        if (r > 0 && rEnabled)
        {
            BitCursor dR{startR / 8, startR % 8};
            for (size_t b = 0; b < r; ++b)
            {
                if (isExtract)
                {
                    const uint8_t bit = dR.get(cur);
                    cR.set(streamR, bit);
                }
                else
                {
                    const uint8_t bit = cR.get(streamR);
                    dR.set(cur, bit);
                }
                dR.advance();
                cR.advance();
            }
        }

        // Remaining X bits are untouched by design.
    }
}

/**
 * @brief Derive three 128-bit AES keys (K_r, K_q, K_p) and three 128-bit IVs
 *        (IV_r IV_q, IV_p) from three bitstreams P, Q, R.
 *
 * Non-consuming:
 *   - Uses only the first 128 bits (16 bytes) of each stream.
 *   - Does NOT modify the input vectors.
 *
 * Keys:
 *   - K_r: random 128-bit key via CryptoPP::AutoSeededRandomPool.
 *   - K_q: first 16 bytes of SHA256( blockR || AES_{K_r}(blockR) ).
 *   - K_p: first 16 bytes of SHA256( blockQ || AES_{K_q}(blockQ) ).
 *
 * IVs:
 *   - IV_r: random 128-bit value.
 *   - IV_q: first 16 bytes of SHA256( Enc_{K_r}(IV_r) || IV_r ).
 *   - IV_p: first 16 bytes of SHA256( Enc_{K_q}(IV_q) || IV_q ).
 *
 * Return:
 *   - result[0] = K_r, result[1] = IV_r,
 *   - result[2] = K_q, result[3] = IV_q
 *   - result[4] = K_p, result[5] = IV_p
 */
std::array<CryptoPP::SecByteBlock, 6> keyGen_PQR(const std::vector<uint8_t> &streamP, const std::vector<uint8_t> &streamQ, const std::vector<uint8_t> &streamR);

void encryptAESCFB_128b(uint8_t *buf, const size_t len, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, const bool isEncryption);

////////////////////////////////////////////////////////////////
////////                    DEFORMERS                    ///////
////////////////////////////////////////////////////////////////

// ---- Base interface for any [0,1] → [0,1] transform ----
class TransformBase
{
public:
    virtual float operator()(float t) const = 0;
    virtual ~TransformBase() = default;
};

// ---- Monotonic / Easing ----
class TransformMonotonic final : public TransformBase
{
private:
    SimpleCryptoPRNG &prng_;
    float alpha_, beta_;
    bool flip_t_, flip_y_, use_surrogate_;
    static inline float rational_surrogate(float t, float a, float b);

public:
    TransformMonotonic(SimpleCryptoPRNG &prng, float amin, float amax, float bmin, float bmax, bool use_surrogate = false);

    static TransformMonotonic fromConfig(SimpleCryptoPRNG &prng, const Configuration &cfg)
    {
        const auto &p = cfg.mono;
        return TransformMonotonic(prng, p.aMin, p.aMax, p.bMin, p.bMax, /*use_surrogate=*/false);
    }

    float operator()(float t) const override;

    static std::string name() { return "Monotonic"; };
    static std::vector<std::pair<std::string, std::string>> cfgParamPairs(const Configuration &cfg)
    {
        return {
            {"alphaMin", std::to_string(cfg.mono.aMin)},
            {"alphaMax", std::to_string(cfg.mono.aMax)},
            {"betaMin", std::to_string(cfg.mono.bMin)},
            {"betaMax", std::to_string(cfg.mono.bMax)},
        };
    }
    std::vector<std::pair<std::string, std::string>> instanceParamPairs(const std::string &before) const
    {
        return {
            {before + "alpha", std::to_string(alpha_)},
            {before + "beta", std::to_string(beta_)},
            {before + "flip_t", use_surrogate_ ? "true" : "false"},
            {before + "flip_y", flip_y_ ? "true" : "false"},
            {before + "use_surrogate", use_surrogate_ ? "true" : "false"},
        };
    }
};

// ---- Oscillatory / Chirp ----
class TransformOscillator final : public TransformBase
{
private:
    SimpleCryptoPRNG &prng_;
    float A_, f0_, f1_, d_, phi_;
    bool flip_t_, flip_y_;

public:
    /// Oscillator‑based [0,1]→[0,1] transform (linear chirp + damping)
    ///
    /// @param encodeKey   seed for noise/permutation table (keys the phase pattern)
    /// @param context     extra integer to de‑correlate multiple transforms (e.g. 0,1,2…)
    /// @param f0min       ≥0: minimum start frequency (cycles per unit‑t; period = 1/f0min)
    /// @param f0max       ≥f0min: maximum start frequency (cycles per unit‑t)
    /// @param f1_cycles   ≥1: multiplier M of baseline cycles.
    ///                     Computes end frequency f1 = (2·M–1)·f0, so total cycles=(f0+f1)/2 = M·f0
    ///                     (M=1 ⇒ no extra cycles; M=2 ⇒ double cycles; etc.)
    /// @param dmin        ≥0: minimum damping constant (d=0 ⇒ no decay)
    /// @param dmax        ≥dmin: maximum damping constant
    ///                     dmax = ln(A/(2 ε)) ensures envelope ½A e^(−d t) stays above ε until t=1
    ///                     (e.g. A=1, ε=0.05 ⇒ dmax≈ln(10)≈2.3)
    TransformOscillator(SimpleCryptoPRNG &prng, float f0min, float f0max, float f1_cycles, float dmin, float dmax);

    static TransformOscillator fromConfig(SimpleCryptoPRNG &prng, const Configuration &cfg)
    {
        const auto &o = cfg.osci;
        return TransformOscillator(prng, o.f0Min, o.f0Max, o.f1Cycles, 0.0, o.dMax);
    }

    float operator()(float t) const override;

    static std::string name() { return "Oscillatory"; }
    static std::vector<std::pair<std::string, std::string>> cfgParamPairs(const Configuration &cfg)
    {
        const auto &o = cfg.osci;
        return {
            {"f0Min", std::to_string(o.f0Min)},
            {"f0Max", std::to_string(o.f0Max)},
            {"f1Cycles", std::to_string(o.f1Cycles)},
            {"dMax", std::to_string(o.dMax)},
        };
    }
    std::vector<std::pair<std::string, std::string>> instanceParamPairs(const std::string &before) const
    {
        return {
            {before + "A", std::to_string(A_)},
            {before + "f0", std::to_string(f0_)},
            {before + "f1", std::to_string(f1_)},
            {before + "d", std::to_string(d_)},
            {before + "phi", std::to_string(phi_)},
            {before + "flip_t", flip_t_ ? "true" : "false"},
            {before + "flip_y", flip_y_ ? "true" : "false"},
        };
    }
};

////////////////////////////////////////////////////////////////
////////                    INTERVALS                    ///////
////////////////////////////////////////////////////////////////

inline void validate(bool cond, const char *msg)
{
    if (!cond)
    {
        std::cerr << msg << "\n";
        std::exit(1);
    }
}

std::array<std::pair<float, float>, 3> randFloatLinUniform(SimpleCryptoPRNG &prng, float minA, float minB, float idGap);

#endif