// meshTools.h
#pragma once

#include <array>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "gnuplot-iostream.h"
#include "happly.h"
#include "nanoflann.hpp"
#include "securityTools.h"

// Data for bounding box and ranges
struct MinMaxData
{
    std::array<float, 3> minXYZ; // per-axis minimums
    std::array<float, 3> maxXYZ; // per-axis maximums
    float globalMin;             // overall minimum
    float globalMax;             // overall maximum
    float minRange;              // smallest axis range
    float maxRange;              // largest axis range
};

// Data for normalization
struct UnitXform
{
    std::array<double, 3> shift; // what you subtracted
    double scale;                // s = 1.0 / r (store as double to reduce drift)
    bool centered;               // just for bookkeeping
};

class Mesh
{
private:
    // per-vertex data
    std::vector<float> vertices_;  // x,y,z per vertex
    std::vector<float> normals_;   // nx,ny,nz per vertex (may be empty)
    std::vector<float> texcoords_; // u,v per vertex (may be empty)

    // per-face indices
    std::vector<std::array<size_t, 3>> faces_;            // vertex indices
    std::vector<std::array<size_t, 3>> normal_indices_;   // normal indices
    std::vector<std::array<size_t, 3>> texcoord_indices_; // texcoord indices

    // OBJ material libraries and names
    std::vector<int> face_material_ids_;      // material id per face
    std::vector<std::string> mtllibs_;        // lines "mtllib ..."
    std::vector<std::string> material_names_; // from tinyobj materials

    // file metadata
    bool is_obj_ = false;                 // OBJ or PLY
    std::vector<std::string> obj_header_; // Material OBJ

    // internal load/save
    void loadPLY_(const std::string &path);
    void loadOBJ_(const std::string &path);
    void savePLY(const std::string &path) const;
    void saveOBJ(const std::string &path) const;

    Mesh() = default;

    // Laplacian Smoothing helper
    std::vector<std::vector<size_t>> buildAdjacency() const;
    void laplaceStepUniform_(const std::vector<std::vector<size_t>> &adj, double s);

    // Cotangent-weighted Laplacian helpers
    struct NeighborW
    {
        size_t j;
        double w;
    };
    std::vector<std::vector<NeighborW>> buildCotanAdjacency_() const;
    static inline double cotangentAt_(const float *vi, const float *vj, const float *vk);
    void laplaceStepCotan_(const std::vector<std::vector<NeighborW>> &adjW, double s);

public:
    // Constructor / Save / Clone
    explicit Mesh(const std::string &path);
    Mesh(const Mesh &) = default;
    void save(const std::string &path) const;
    Mesh &operator=(const Mesh &) = default;
    Mesh clone() const;

    size_t getSizeVertices() const { return this->vertices_.size(); }

    // Geodesic vertex reordering
    void reorderByEuclideanRadius(SimpleCryptoPRNG &prng);

    // Deform (encode/decode) with per-axis interpolators
    // interpX/Y/Z: functor(float t)->float in [0,1]
    // decode=false: multiply; true: divide
    template <class TX, class TY, class TZ>
    void deform(const std::array<std::pair<float, float>, 3> &scales, TX &&ix, TY &&iy, TZ &&iz, bool decode)
    {
        size_t N = vertices_.size() / 3;
        if (N < 2)
            return;
        auto [s0x, e1x] = scales[0];
        float dx = e1x - s0x;
        auto [s0y, e1y] = scales[1];
        float dy = e1y - s0y;
        auto [s0z, e1z] = scales[2];
        float dz = e1z - s0z;
        float invN = 1.0f / float(N - 1);
        for (size_t i = 0; i < N; ++i)
        {
            float t = i * invN;
            float fx = s0x + dx * ix(t);
            float fy = s0y + dy * iy(t);
            float fz = s0z + dz * iz(t);
            float *v = &vertices_[3 * i];
            if (!decode)
            {
                v[0] *= fx;
                v[1] *= fy;
                v[2] *= fz;
            }
            else
            {
                v[0] /= fx;
                v[1] /= fy;
                v[2] /= fz;
            }
        }
    }

    // Quantize/dequantize in place (qp = quantization bits [5..20])
    void quantizeDequantizeInPlace(int qp);

    // Laplacian Taubin smoothing
    void taubinSmoothInPlace(int iterations, double lambda = 0.5, double mu = -0.53);
    void taubinSmoothInPlaceIte(int iterations, double lambda, double mu, const std::vector<std::vector<size_t>> &adjacency);
    void taubinSmoothCotanInPlace(int iterations, double lambda = 0.5, double mu = -0.53);
    void taubinSmoothCotanInPlaceIte(int iterations, double lambda, double mu, const std::vector<std::vector<NeighborW>> &adjW);

    // Normalize to unit cube and back
    UnitXform computeUnitXform(bool center = true) const;
    void applyUnitInPlace(const UnitXform &xf);
    void applyInverseUnitInPlace(const UnitXform &xf);

    // Edge lengths
    std::vector<double> edgeLengths() const;

    // Statistical measures
    MinMaxData computeMinMax() const;
    double rmse(const Mesh &o) const;
    double hausdorff(const Mesh &o) const;
    double npcr(const Mesh &o) const;
    double uaci(const Mesh &o) const;
    double entropy() const;
    std::array<double, 4> pearsonCorrelation(const Mesh &o) const;
    std::array<double, 4> autoCorrelationLag1Index() const;
    double autoCorrelationLag1Seed(size_t seedIdx) const;

    // Gnuplot visualizations
    void plotHistPoints(Gnuplot &gp, const std::string &out, int bins = 127) const;
    void plotHistEdges(Gnuplot &gp, const std::string &out, int bins = 127) const;
    void plotPointCorrIndex(Gnuplot &gp, const std::string &out) const;
    void plotPointCorrSeed(Gnuplot &gp, const std::string &out, size_t seedIdx) const;

    // nanoflann interface
    template <class BBOX>
    bool kdtree_get_bbox(BBOX &bb) const;
    size_t kdtree_get_point_count() const;
    float kdtree_get_pt(size_t idx, size_t dim) const;

    // Selective Encryption
    void selectEncrAESCFB(int nbBitsEncr, int nbBitsOffset, bool isEncryption, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv);

    // Lorenz 3D encryption
    void fullEncrLorenz3D(bool isEncryption, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv);

    // Hierarchical 3D encryption
    void processBits(uint8_t *const streamP, uint8_t *const streamQ, uint8_t *const streamR, const size_t p, const size_t q, const size_t r, const bool isExtract, const bool pEnabled, const bool qEnabled, const bool rEnabled);
};

// Utility
inline bool floatEqual(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) < eps; }
void initGnuplotParamAndStyle(Gnuplot &gp);