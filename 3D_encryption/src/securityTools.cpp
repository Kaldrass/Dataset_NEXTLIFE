#include "securityTools.h"

////////////////////////////////////////////////////////////////
////////                      KEYS                       ///////
////////////////////////////////////////////////////////////////

std::string encodeHexStringSecByteBlock(const CryptoPP::SecByteBlock &block)
{
  std::string res;
  CryptoPP::StringSource(block, block.size(), true, new CryptoPP::HexEncoder(new CryptoPP::StringSink(res)));
  return res;
}

CryptoPP::SecByteBlock decodeHexStringSecByteBlock(const std::string &blockString)
{
  size_t blockSize = blockString.size() / 2;
  CryptoPP::SecByteBlock keyByte(blockSize);
  CryptoPP::StringSource(blockString, true, new CryptoPP::HexDecoder(new CryptoPP::ArraySink(keyByte, keyByte.size())));
  return keyByte;
}

////////////////////////////////////////////////////////////////
////////                      CORE                       ///////
////////////////////////////////////////////////////////////////

void checkOverflowParams(size_t nbBitsTarget, size_t nbBitsOffset, size_t sizeByteType)
{
  if (sizeByteType == 0 || nbBitsTarget == 0)
  {
    std::cerr << "[checkOverflowParams] - ERROR: sizeByteType must be > 0 ; nbBitsTarget must be > 0" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  if (nbBitsTarget + nbBitsOffset > sizeByteType * 8)
  {
    std::cerr << "[checkOverflowParams] - ERROR: nbBitsTarget + nbBitsOffset exceeds data type capacity (" << (sizeByteType * 8) << " bits)." << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

////////////////////////////////////////////////////////////////
////////              LORENZ 3D ENCRYPTION               ///////
////////////////////////////////////////////////////////////////

std::array<double, 3> lorenz_deriv(const std::array<double, 3> &s, double sigma, double rho, double beta)
{
  return {sigma * (s[1] - s[0]), s[0] * (rho - s[2]) - s[1], s[0] * s[1] - beta * s[2]};
}

void lorenz_rk4_step(std::array<double, 3> &s, double dt)
{
  const auto k1 = lorenz_deriv(s);
  const std::array<double, 3> a{s[0] + 0.5 * dt * k1[0], s[1] + 0.5 * dt * k1[1], s[2] + 0.5 * dt * k1[2]};
  const auto k2 = lorenz_deriv(a);
  const std::array<double, 3> b{s[0] + 0.5 * dt * k2[0], s[1] + 0.5 * dt * k2[1], s[2] + 0.5 * dt * k2[2]};
  const auto k3 = lorenz_deriv(b);
  const std::array<double, 3> c{s[0] + dt * k3[0], s[1] + dt * k3[1], s[2] + dt * k3[2]};
  const auto k4 = lorenz_deriv(c);

  s[0] += (dt / 6.0) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
  s[1] += (dt / 6.0) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
  s[2] += (dt / 6.0) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
}

void derive_lorenz_initials_from_key(const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, double &x0, double &y0, double &z0)
{
  using namespace CryptoPP;
  HKDF<SHA256> hkdf;
  const byte info[] = "lorenz-initials-v1";
  byte okm[24]; // 3 * 8 bytes

  hkdf.DeriveKey(okm, sizeof(okm), key.BytePtr(), key.size(), iv.BytePtr(), iv.size(), info, sizeof(info) - 1);

  auto rd64 = [&](int i) -> uint64_t
  {
    uint64_t u = 0;
    for (int b = 0; b < 8; ++b)
      u |= uint64_t(okm[8 * i + b]) << (8 * b); // little-endian
    return u;
  };
  const uint64_t r0 = rd64(0), r1 = rd64(1), r2 = rd64(2);

  // Friendly ranges for Lorenz chaos
  x0 = lerp(u01_from_u64(r0), -15.0, 15.0);
  y0 = lerp(u01_from_u64(r1), -15.0, 15.0);
  z0 = lerp(u01_from_u64(r2), 0.0, 35.0);
}

void generate_lorenz_keys_3f(std::vector<float> &K, size_t Nverts, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv)
{
  K.resize(3 * Nverts);

  constexpr double dt = 0.01;
  constexpr int burnin = 5000;
  constexpr double clipM = 30.0;
  constexpr double Ax = 1.0, Ay = 1.0, Az = 1.0;

  double x0, y0, z0;
  derive_lorenz_initials_from_key(key, iv, x0, y0, z0);

  std::array<double, 3> s{x0, y0, z0};
  for (int i = 0; i < burnin; ++i)
    lorenz_rk4_step(s, dt);

  auto map1 = [&](double v) -> double
  {
    double vv = v;
    if (vv > clipM)
      vv = clipM;
    if (vv < -clipM)
      vv = -clipM;
    return vv / clipM; // [-1,1]
  };

  for (size_t i = 0; i < Nverts; ++i)
  {
    lorenz_rk4_step(s, dt);
    K[3 * i + 0] = static_cast<float>(Ax * map1(s[0]));
    K[3 * i + 1] = static_cast<float>(Ay * map1(s[1]));
    K[3 * i + 2] = static_cast<float>(Az * map1(s[2]));
  }
}

void computeSolo_LorenzAffine(float *verts, size_t count, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, bool isEncryption)
{
  if (!verts || count % 3 != 0)
    return;

  const size_t N = count / 3;

  std::vector<float> K;
  generate_lorenz_keys_3f(K, N, key, iv); // fills K[3*i + {0,1,2}]
  constexpr double W = 0.5;

  if (isEncryption)
  {
    for (size_t i = 0; i < N; ++i)
    {
      float *v = &verts[3 * i];
      v[0] = static_cast<float>(W * double(v[0]) + K[3 * i + 0]);
      v[1] = static_cast<float>(W * double(v[1]) + K[3 * i + 1]);
      v[2] = static_cast<float>(W * double(v[2]) + K[3 * i + 2]);
    }
  }
  else
  {
    for (size_t i = 0; i < N; ++i)
    {
      float *v = &verts[3 * i];
      v[0] = static_cast<float>((double(v[0]) - K[3 * i + 0]) / W);
      v[1] = static_cast<float>((double(v[1]) - K[3 * i + 1]) / W);
      v[2] = static_cast<float>((double(v[2]) - K[3 * i + 2]) / W);
    }
  }
}

////////////////////////////////////////////////////////////////
////////             HIERARCHICAL ENCRYPTION             ///////
////////////////////////////////////////////////////////////////

std::array<CryptoPP::SecByteBlock, 6> keyGen_PQR(const std::vector<uint8_t> &streamP, const std::vector<uint8_t> &streamQ, const std::vector<uint8_t> &streamR)
{
  // We need at least 128 bits (16 bytes) from each stream.
  constexpr size_t blockBytes = CryptoPP::AES::BLOCKSIZE; // 16 bytes = 128 bits
  if (streamP.size() < blockBytes || streamQ.size() < blockBytes || streamR.size() < blockBytes)
  {
    std::cerr << "[keyGen_PQR] - ERROR: one of the streams (streamP=" << streamP.size() << " | streamQ=" << streamQ.size() << " | streamR=" << streamR.size() << ") is shorter than " << blockBytes << " bytes.\n";
    std::exit(EXIT_FAILURE);
  }

  // Extract first 128 bits (16 bytes = blockBytes) from each stream.
  CryptoPP::SecByteBlock blockP(blockBytes);
  CryptoPP::SecByteBlock blockQ(blockBytes);
  CryptoPP::SecByteBlock blockR(blockBytes);
  std::memcpy(blockP.data(), streamP.data(), blockBytes);
  std::memcpy(blockQ.data(), streamQ.data(), blockBytes);
  std::memcpy(blockR.data(), streamR.data(), blockBytes);

  // --- Keys ---

  // K_r: random 128-bit key
  CryptoPP::AutoSeededRandomPool rnd;
  CryptoPP::SecByteBlock K_r(blockBytes);
  rnd.GenerateBlock(K_r.data(), K_r.size());

  // K_q from (R, K_r)
  CryptoPP::AES::Encryption aesK_r;
  aesK_r.SetKey(K_r.data(), K_r.size());

  CryptoPP::SecByteBlock blockR_enc(blockBytes);
  aesK_r.ProcessBlock(blockR.data(), blockR_enc.data());

  CryptoPP::SecByteBlock concatR(blockBytes * 2); // blockR || blockR_enc
  std::memcpy(concatR.data(), blockR.data(), blockBytes);
  std::memcpy(concatR.data() + blockBytes, blockR_enc.data(), blockBytes);

  CryptoPP::SHA256 hashR;
  CryptoPP::SecByteBlock digestR(hashR.DigestSize()); // 32-byte digest
  hashR.CalculateDigest(digestR.data(), concatR.data(), concatR.size());

  CryptoPP::SecByteBlock K_q(blockBytes);
  std::memcpy(K_q.data(), digestR.data(), blockBytes); // first 16 bytes

  // K_p from (Q, K_q)
  CryptoPP::AES::Encryption aesK_q;
  aesK_q.SetKey(K_q.data(), K_q.size());

  CryptoPP::SecByteBlock blockQ_enc(blockBytes);
  aesK_q.ProcessBlock(blockQ.data(), blockQ_enc.data());

  CryptoPP::SecByteBlock concatQ(blockBytes * 2); // blockQ || blockQ_enc
  std::memcpy(concatQ.data(), blockQ.data(), blockBytes);
  std::memcpy(concatQ.data() + blockBytes, blockQ_enc.data(), blockBytes);

  CryptoPP::SHA256 hashQ;
  CryptoPP::SecByteBlock digestQ(hashQ.DigestSize());
  hashQ.CalculateDigest(digestQ.data(), concatQ.data(), concatQ.size());

  CryptoPP::SecByteBlock K_p(blockBytes);
  std::memcpy(K_p.data(), digestQ.data(), blockBytes); // first 16 bytes

  // --- IVs ---

  // IV_r: random 128-bit
  CryptoPP::SecByteBlock IV_r(blockBytes);
  rnd.GenerateBlock(IV_r.data(), IV_r.size());

  // IV_q from (IV_r, K_r): SHA256( Enc_{K_r}(IV_r) || IV_r )
  CryptoPP::SecByteBlock IV_r_enc(blockBytes);
  aesK_r.ProcessBlock(IV_r.data(), IV_r_enc.data());

  CryptoPP::SecByteBlock concatIVr(blockBytes * 2);
  std::memcpy(concatIVr.data(), IV_r.data(), blockBytes);
  std::memcpy(concatIVr.data() + blockBytes, IV_r_enc.data(), blockBytes);

  CryptoPP::SHA256 hashIVq;
  CryptoPP::SecByteBlock digestIVq(hashIVq.DigestSize());
  hashIVq.CalculateDigest(digestIVq.data(), concatIVr.data(), concatIVr.size());

  CryptoPP::SecByteBlock IV_q(blockBytes);
  std::memcpy(IV_q.data(), digestIVq.data(), blockBytes); // first 16 bytes

  // IV_p from (IV_q, K_q): SHA256( Enc_{K_q}(IV_q) || IV_q )
  CryptoPP::SecByteBlock IV_q_enc(blockBytes);
  aesK_q.ProcessBlock(IV_q.data(), IV_q_enc.data());

  CryptoPP::SecByteBlock concatIVq(blockBytes * 2);
  std::memcpy(concatIVq.data(), IV_q.data(), blockBytes);
  std::memcpy(concatIVq.data() + blockBytes, IV_q_enc.data(), blockBytes);

  CryptoPP::SHA256 hashIVp;
  CryptoPP::SecByteBlock digestIVp(hashIVp.DigestSize());
  hashIVp.CalculateDigest(digestIVp.data(), concatIVq.data(), concatIVq.size());

  CryptoPP::SecByteBlock IV_p(blockBytes);
  std::memcpy(IV_p.data(), digestIVp.data(), blockBytes); // first 16 bytes

  return std::array<CryptoPP::SecByteBlock, 6>{std::move(K_r), std::move(IV_r), std::move(K_p), std::move(IV_q), std::move(K_q), std::move(IV_p)};
}

void encryptAESCFB_128b(uint8_t *buf, const size_t len, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv, const bool isEncryption)
{
  // Sanity check Key/IV
  constexpr size_t blockBytes = CryptoPP::AES::BLOCKSIZE; // 16 bytes
  if (!CryptoPP::AES::Encryption().IsValidKeyLength(key.size()))
  {
    std::cerr << "[aes_cfb128_transform_stream] - ERROR: invalid AES key length (" << key.size() << ").\n";
    std::exit(EXIT_FAILURE);
  }
  if (iv.size() != blockBytes)
  {
    std::cerr << "[aes_cfb128_transform_stream] - ERROR: IV size is " << iv.size() << ", expected " << blockBytes << " bytes.\n";
    std::exit(EXIT_FAILURE);
  }

  CryptoPP::AES::Encryption aes;
  aes.SetKey(key.data(), key.size());

  // Feedback register R (128 bits), initialized with IV
  CryptoPP::SecByteBlock R(blockBytes);
  std::memcpy(R.data(), iv.data(), blockBytes);

  size_t offset = 0;
  while (offset < len)
  {
    const size_t remaining = len - offset;
    const size_t blockLen = (remaining >= blockBytes) ? blockBytes : remaining;

    // Keystream = E_K(R)
    uint8_t keystream[blockBytes];
    aes.ProcessBlock(R.data(), keystream);

    uint8_t Ctemp[blockBytes] = {0}; // store ciphertext for feedback

    if (isEncryption)
    {
      // C = P XOR KS
      for (size_t i = 0; i < blockLen; ++i)
      {
        const uint8_t p = buf[offset + i];
        const uint8_t c = static_cast<uint8_t>(p ^ keystream[i]);
        buf[offset + i] = c;
        Ctemp[i] = c;
      }
    }
    else
    {
      // P = C XOR KS (but feedback uses C)
      for (size_t i = 0; i < blockLen; ++i)
      {
        const uint8_t c = buf[offset + i];
        Ctemp[i] = c;
        const uint8_t p = static_cast<uint8_t>(c ^ keystream[i]);
        buf[offset + i] = p;
      }
    }

    // Feedback update:
    // - Full block: R = C
    // - Partial: shift R left by blockLen bytes and append C at the end.
    if (blockLen == blockBytes)
    {
      std::memcpy(R.data(), Ctemp, blockBytes);
    }
    else
    {
      // Shift left: drop oldest blockLen bytes, keep last (blockBytes - blockLen)
      std::memmove(R.data(), R.data() + blockLen, blockBytes - blockLen);
      std::memcpy(R.data() + (blockBytes - blockLen), Ctemp, blockLen);
    }

    offset += blockLen;
  }
}

////////////////////////////////////////////////////////////////
////////                    DEFORMERS                    ///////
////////////////////////////////////////////////////////////////

// ---- Monotonic / Easing ----
inline float TransformMonotonic::rational_surrogate(float t, float a, float b)
{
  float num = std::pow(t, a);
  float den = num + std::pow(1.0f - t, b);
  return num / den;
}

TransformMonotonic::TransformMonotonic(SimpleCryptoPRNG &prng, float amin, float amax, float bmin, float bmax, bool use_surrogate) : prng_(prng), use_surrogate_(use_surrogate)
{
  if (amin <= 0.0f)
    throw std::invalid_argument("[ERROR] - TransformMonotonic: amin must be > 0 (amin=" + std::to_string(amin) + ")");
  if (bmin <= 0.0f)
    throw std::invalid_argument("[ERROR] - TransformMonotonic: bmin must be > 0 (bmin=" + std::to_string(bmin) + ")");
  if (amax < amin)
    throw std::invalid_argument("[ERROR] - TransformMonotonic: amax must be >= amin (amax=" + std::to_string(amax) + ", amin=" + std::to_string(amin) + ")");
  if (bmax < bmin)
    throw std::invalid_argument("[ERROR] - TransformMonotonic: bmax must be >= bmin (bmax=" + std::to_string(bmax) + ", bmin=" + std::to_string(bmin) + ")");

  flip_t_ = prng_.nextBool();
  flip_y_ = prng_.nextBool();
  alpha_ = prng_.nextFloat(amin, amax);
  beta_ = prng_.nextFloat(bmin, bmax);
}
float TransformMonotonic::operator()(float t) const
{
  float u = flip_t_ ? 1.0f - t : t;
  float y = use_surrogate_ ? rational_surrogate(u, alpha_, beta_) : boost::math::ibeta(alpha_, beta_, u);
  return flip_y_ ? 1.0f - y : y;
}

// ---- Oscillatory / Chirp ----
TransformOscillator::TransformOscillator(SimpleCryptoPRNG &prng, float f0min, float f0max, float f1_cycles, float dmin, float dmax) : prng_(prng)
{
  if (dmin < 0.0f)
    throw std::invalid_argument("[ERROR] - TransformOscillator: dmin must be >= 0 (dmin=" + std::to_string(dmin) + ")");
  if (f0min <= 0.0f)
    throw std::invalid_argument("[ERROR] - TransformOscillator: f0min must be > 0 (f0min=" + std::to_string(f0min) + ")");
  if (f0max < f0min)
    throw std::invalid_argument("[ERROR] - TransformOscillator: f0max must be >= f0min (f0max=" + std::to_string(f0max) + ", f0min=" + std::to_string(f0min) + ")");
  if (f1_cycles < 1.0f)
    throw std::invalid_argument("[ERROR] - TransformOscillator: f1_cycles must be > 1 (f1_cycles=" + std::to_string(f1_cycles) + ")");
  if (dmax < dmin)
    throw std::invalid_argument("[ERROR] - TransformOscillator: dmax must be >= dmin (dmax=" + std::to_string(dmax) + ", dmin=" + std::to_string(dmin) + ")");

  A_ = 1.0f; // Fixed for now (must be below 1.0 anyways)
  flip_t_ = prng_.nextBool();
  flip_y_ = prng_.nextBool();
  f0_ = prng_.nextFloat(f0min, f0max);
  f1_ = prng_.nextFloat(f0_, (2.0f * f1_cycles * f0_ - f0_));
  d_ = prng_.nextFloat(dmin, dmax);
  phi_ = prng_.nextFloat(0.0f, 2.0f * static_cast<float>(M_PI));
}
float TransformOscillator::operator()(float t) const
{
  float u = flip_t_ ? 1.0f - t : t;
  float phase = 2 * M_PI * (f0_ * u + 0.5f * (f1_ - f0_) * u * u) + phi_;
  float env = std::exp(-d_ * u);
  float raw = A_ * env * std::sin(phase);
  float y = 0.5f + 0.5f * raw;
  return flip_y_ ? 1.0f - y : y;
}

////////////////////////////////////////////////////////////////
////////                    INTERVALS                    ///////
////////////////////////////////////////////////////////////////

/**
 * Samples three (low, high) pairs of floats so that each pair “straddles” 1.0,
 * using a purely log‐domain approach:
 *
 *   • We force “low = s” and “high = 1/s” exactly (so s < 1 < 1/s).
 *   • We draw x_log ∼ Uniform[ ln(minA), ln(min(maxB, 1 - idGap)) ].
 *   • Then s = exp(x_log), high = exp(-x_log).  That is a log‐uniform distribution.
 *   • Finally we shuffle the three high‐values among the three low‐values and
 *     randomly flip each pair’s order.
 *
 * @param prng   Cryptographically strong PRNG with methods:
 *                 - nextFloat(a,b): uniform float in [a, b]
 *                 - nextInt(a,b):   uniform int in [a, b) (upper‐bound exclusive)
 *                 - nextBool():     uniform bool
 * @param minA   Lower bound on “below 1” side, in linear domain, 0 < minA < 1.
 * @param minB   Secondary bound on “below 1” side, in linear domain, 0 < minA ≤ minB < 1.
 * @param idGap  Gap parameter, 0 < idGap < 1, ensuring (1 − idGap) > minA.
 *
 * @return An array of three pairs (float, float).  Each pair either is (low,high) or (high,low),
 *         chosen at random, but always low < 1 < high and low * high == 1 exactly.
 */
std::array<std::pair<float, float>, 3> randFloatLinUniform(SimpleCryptoPRNG &prng, float minA, float minB, float idGap)
{
  // STEP 0: Validate Inputs (all in (0,1), with minA ≤ minB and minA < 1-idGap)
  validate(minA > 0.0f, "[ERROR] - minA must be > 0.0");
  validate(minB > 0.0f, "[ERROR] - minB must be > 0.0");
  validate(minA <= minB, "[ERROR] - minA must be ≤ minB");
  validate(minA < 1.0f, "[ERROR] - minA must be < 1.0");
  validate(minB < 1.0f, "[ERROR] - minB must be < 1.0");
  validate(idGap > 0.0f, "[ERROR] - idGap must be > 0.0");
  validate(idGap < 1.0f, "[ERROR] - idGap must be < 1.0");
  validate((1.0f - idGap) > minA, "[ERROR] - minA must be < (1.0 - idGap)");

  // STEP 1: Compute the linear upper bound for “low”
  float maxLow = std::min(minB, 1.0f - idGap);
  // Sanity‐check: must have minA < maxLow
  if (!(minA < maxLow))
  {
    std::cerr << "[ERROR] - No valid linear interval for sampling.\n";
    std::exit(1);
  }

  // STEP 2: Draw 3 independent (low, high) pairs
  //   low  = s ∼ Uniform(minA, maxLow)
  //   high = 1.0f/s
  std::array<float, 3> lows, highs;
  for (int i = 0; i < 3; ++i)
  {
    float s = prng.nextFloat(minA, maxLow);
    float inv_s = 1.0f / s; // partner > 1

    lows[i] = s;
    highs[i] = inv_s;
  }

  // STEP 3: In‐place shuffle of highs[] (Fisher–Yates)
  for (int i = 2; i > 0; --i)
  {
    int j = prng.nextInt(0, i + 1); // uniform in [0..i]
    std::swap(highs[i], highs[j]);
  }

  // STEP 4: Random 50/50 orientation for each pair
  std::array<std::pair<float, float>, 3> result;
  for (int i = 0; i < 3; ++i)
  {
    if (prng.nextBool())
    {
      result[i] = {lows[i], highs[i]}; // (low < 1, high > 1)
    }
    else
    {
      result[i] = {highs[i], lows[i]}; // (high > 1, low < 1)
    }
  }
  return result;
}