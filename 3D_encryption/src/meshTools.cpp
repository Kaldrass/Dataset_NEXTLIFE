// meshTools.cpp
#include "tiny_obj_loader.h"
#include "meshTools.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

///////////////////////////////////////////
////////  CONSTRUCT / SAVE / LOAD  ////////
///////////////////////////////////////////

Mesh::Mesh(const std::string &path)
{
    std::filesystem::path fp(path);
    if (!std::filesystem::is_regular_file(fp))
    {
        std::cerr << "[Mesh] ERROR: Not a regular file: " << path << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::string ext = fp.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                   { return std::tolower(c); });

    if (ext == ".ply")
    {
        is_obj_ = false;
        loadPLY_(path);
    }
    else if (ext == ".obj")
    {
        is_obj_ = true;
        loadOBJ_(path);
    }
    else
    {
        std::cerr << "[Mesh] ERROR: Unsupported extension '" << ext << "'" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void Mesh::save(const std::string &path) const
{
    if (!path.empty())
        is_obj_ ? saveOBJ(path) : savePLY(path);
}

Mesh Mesh::clone() const
{
    Mesh m;
    m.vertices_ = vertices_;
    m.normals_ = normals_;
    m.texcoords_ = texcoords_;
    m.faces_ = faces_;
    m.normal_indices_ = normal_indices_;
    m.texcoord_indices_ = texcoord_indices_;
    m.face_material_ids_ = face_material_ids_;
    m.mtllibs_ = mtllibs_;
    m.material_names_ = material_names_;
    m.obj_header_ = obj_header_;
    m.is_obj_ = is_obj_;
    return m;
}

void Mesh::loadPLY_(const std::string &path)
{
    if (!path.empty())
    {
        happly::PLYData data(path.c_str());
        data.getVertexPositionsFloat(vertices_);
        data.getTriangleIndices(faces_);

        // no normal, no texcoords or materials in PLY
        normals_.clear();
        texcoords_.clear();
        normal_indices_.clear();
        texcoord_indices_.clear();
        face_material_ids_.clear();
        mtllibs_.clear();
        material_names_.clear();
        obj_header_.clear();
    }
    else
    {
        std::cerr << "[ERROR] - Path loadPLY empty." << std::endl;
        std::exit(1);
    }
}

void Mesh::savePLY(const std::string &path) const
{
    if (path.empty())
        return;

    std::string out = path;
    if (out.find_last_of('.') == std::string::npos || out.substr(out.find_last_of('.')) != ".ply")
        out += ".ply";

    happly::PLYData ply;
    ply.addVertexPositionsFloat(vertices_);
    ply.addTriangleIndices(faces_);
    ply.write(out, happly::DataFormat::Binary);
}

void Mesh::loadOBJ_(const std::string &path)
{
    // — 1) capture everything before the first "v " —
    obj_header_.clear();
    std::ifstream ifs(path);
    if (!ifs)
    {
        std::cerr << "[loadOBJ] Cannot open " << path << "\n";
        std::exit(EXIT_FAILURE);
    }
    std::string line;

    mtllibs_.clear();
    obj_header_.clear();
    // read header lines
    while (std::getline(ifs, line))
    {
        if (line.rfind("mtllib ", 0) == 0)
            mtllibs_.push_back(line);
        else if (line.rfind("v ", 0) == 0)
        {
            ifs.seekg(-int(line.size()) - 1, std::ios::cur);
            break;
        }
        else
            obj_header_.push_back(line);
    }

    // parse
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    // derive base directory for mtllib
    std::string base_dir = std::filesystem::path(path).parent_path().string();
    if (!base_dir.empty() && base_dir.back() != '/' && base_dir.back() != '\\')
        base_dir += std::filesystem::path::preferred_separator;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.c_str(), base_dir.empty() ? nullptr : base_dir.c_str(), /*triangulate=*/true);

    if (!err.empty())
        std::cerr << "[loadOBJ] WARN/ERR: " << err << "\n";
    if (!ok)
        std::exit(EXIT_FAILURE);

    // move arrays
    vertices_ = std::move(attrib.vertices);
    normals_ = std::move(attrib.normals);
    texcoords_ = std::move(attrib.texcoords);
    material_names_.reserve(materials.size());
    for (auto &m : materials)
        material_names_.push_back(m.name);

    // clear old
    faces_.clear();
    normal_indices_.clear();
    texcoord_indices_.clear();
    face_material_ids_.clear();

    // build per-face
    for (const auto &shape : shapes)
    {
        size_t off = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
        {
            int fv = shape.mesh.num_face_vertices[f];
            int mid = shape.mesh.material_ids[f];
            if (fv != 3)
            {
                off += fv;
                continue;
            }
            std::array<size_t, 3> vI, tI, nI;
            for (int v = 0; v < 3; ++v)
            {
                auto &idx = shape.mesh.indices[off + v];
                vI[v] = idx.vertex_index;
                tI[v] = idx.texcoord_index;
                nI[v] = idx.normal_index;
            }
            faces_.push_back(vI);
            texcoord_indices_.push_back(tI);
            normal_indices_.push_back(nI);
            face_material_ids_.push_back(mid);
            off += 3;
        }
    }
}

void Mesh::saveOBJ(const std::string &path) const
{
    std::string out = path;
    if (out.find_last_of('.') == std::string::npos || out.substr(out.find_last_of('.')) != ".obj")
        out += ".obj";

    std::ofstream ofs(out);
    if (!ofs)
    {
        std::cerr << "[saveOBJ] Cannot write " << out << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // header: mtllib + other
    for (auto &l : mtllibs_)
        ofs << l << "\n";
    for (auto &l : obj_header_)
        ofs << l << "\n";
    ofs << "\n";

    // v/vn/vt
    for (size_t i = 0; i + 2 < vertices_.size(); i += 3)
        ofs << "v " << vertices_[i] << " " << vertices_[i + 1] << " " << vertices_[i + 2] << "\n";
    if (normals_.size() >= 2)
        for (size_t i = 0; i + 2 < normals_.size(); i += 3)
            ofs << "vn " << normals_[i] << " " << normals_[i + 1] << " " << normals_[i + 2] << "\n";
    if (texcoords_.size() >= 2)
        for (size_t i = 0; i + 1 < texcoords_.size(); i += 2)
            ofs << "vt " << texcoords_[i] << " " << texcoords_[i + 1] << "\n";
    ofs << "\n";

    // faces with usemtl changes
    size_t prev_mid = SIZE_MAX;
    for (size_t f = 0; f < faces_.size(); ++f)
    {
        int mid_i = (f < face_material_ids_.size() ? face_material_ids_[f] : -1);
        size_t mid = (mid_i >= 0 ? static_cast<size_t>(mid_i) : SIZE_MAX);
        if (mid != prev_mid && mid < material_names_.size())
        {
            ofs << "usemtl " << material_names_[mid] << "\n";
            prev_mid = mid;
        }
        auto &F = faces_[f];
        auto &T = texcoord_indices_[f];
        auto &N = normal_indices_[f];
        ofs << "f";
        for (int v = 0; v < 3; ++v)
            ofs << " " << (F[v] + 1) << "/" << (T[v] + 1) << "/" << (N[v] + 1);
        ofs << "\n";
    }
}

///////////////////////////////////////////
////////       GEODESIC PATH       ////////
///////////////////////////////////////////

void Mesh::reorderByEuclideanRadius(SimpleCryptoPRNG &prng)
{
    const size_t V = vertices_.size() / 3;
    if (V == 0)
        throw std::out_of_range("reorderByEuclideanRadius: no vertices");
    if (V > std::numeric_limits<uint32_t>::max())
        throw std::out_of_range("reorderByEuclideanRadius: V too large for 32-bit index");

    // 0) pick a random seed vertex
    const uint32_t seed = prng.nextUInt(0, static_cast<uint32_t>(V));
    const double sx = vertices_[3 * seed + 0];
    const double sy = vertices_[3 * seed + 1];
    const double sz = vertices_[3 * seed + 2];

    // 1) Euclidean radii to seed
    std::vector<double> r(V);
    for (size_t i = 0; i < V; ++i)
    {
        const double dx = vertices_[3 * i + 0] - sx;
        const double dy = vertices_[3 * i + 1] - sy;
        const double dz = vertices_[3 * i + 2] - sz;
        r[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // 2) Order by radius (stable -> deterministic for ties)
    std::vector<size_t> order(V);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                     {
        if (r[a] == r[b]) return a < b;
        return r[a] < r[b]; });

    // 3) Rotate so seed is first (id=0 : Important for correlation plots)
    auto it = std::find(order.begin(), order.end(), size_t(seed));
    if (it != order.end())
        std::rotate(order.begin(), it, order.end());

    // 4) Inverse map
    std::vector<size_t> inv(V);
    for (size_t newI = 0; newI < V; ++newI)
        inv[order[newI]] = newI;

    // 5) Permute vertices
    std::vector<float> newV(3 * V);
    for (size_t newI = 0; newI < V; ++newI)
    {
        const size_t oldI = order[newI];
        newV[3 * newI + 0] = vertices_[3 * oldI + 0];
        newV[3 * newI + 1] = vertices_[3 * oldI + 1];
        newV[3 * newI + 2] = vertices_[3 * oldI + 2];
    }
    vertices_.swap(newV);

    // 6) Permute texcoords (if 1 uv per vertex)
    if (texcoords_.size() / 2 == V)
    {
        std::vector<float> newT(2 * V);
        for (size_t newI = 0; newI < V; ++newI)
        {
            const size_t oldI = order[newI];
            newT[2 * newI + 0] = texcoords_[2 * oldI + 0];
            newT[2 * newI + 1] = texcoords_[2 * oldI + 1];
        }
        texcoords_.swap(newT);
        for (auto &tc : texcoord_indices_)
            for (int c = 0; c < 3; ++c)
                tc[c] = inv[tc[c]];
    }

    // 7) Remap faces
    for (auto &f : faces_)
        for (int c = 0; c < 3; ++c)
            f[c] = inv[f[c]];
}

///////////////////////////////////////////
////////       QUANTIZATION        ////////
///////////////////////////////////////////

void Mesh::quantizeDequantizeInPlace(int qp)
{
    // 1) get bounding‐box data
    MinMaxData mm = computeMinMax();
    float r = mm.maxRange;            // the longest side
    uint32_t levels = (1u << qp) - 1; // 2^qp - 1

    // 2) one pass over all vertices vertices_ is flat: [x0,y0,z0, x1,y1,z1, ...]
    size_t Nf = vertices_.size();
    for (size_t i = 0; i + 2 < Nf; i += 3)
    {
        for (int c = 0; c < 3; ++c)
        {
            // original float
            float v = vertices_[i + c];

            // quantize to integer q
            float normalized = (v - mm.minXYZ[c]) * levels / r;
            uint32_t q = uint32_t(std::floor(normalized + 0.5f));

            // dequantize immediately to float v̂
            float v_hat = float(q) * (r / levels) + mm.minXYZ[c];

            // overwrite original
            vertices_[i + c] = v_hat;
        }
    }
}

///////////////////////////////////////////
////////    LAPLACIAN SMOOTHING    ////////
///////////////////////////////////////////

// Build 1-ring adjacency from triangle faces (no caching)
std::vector<std::vector<size_t>> Mesh::buildAdjacency() const
{
    if (faces_.empty())
        throw std::runtime_error("buildAdjacency: no faces; cannot build 1-ring.");

    const size_t nVerts = vertices_.size() / 3;
    std::vector<std::vector<size_t>> adj(nVerts);

    auto add_edge = [&](size_t a, size_t b)
    {
        if (a == b)
            return;
        auto &v = adj[a];
        if (std::find(v.begin(), v.end(), b) == v.end())
            v.push_back(b);
    };

    for (const auto &f : faces_)
    {
        const size_t a = f[0], b = f[1], c = f[2];
        add_edge(a, b);
        add_edge(b, a);
        add_edge(b, c);
        add_edge(c, b);
        add_edge(c, a);
        add_edge(a, c);
    }
    return adj;
}

// One umbrella Laplacian step using external adjacency
void Mesh::laplaceStepUniform_(const std::vector<std::vector<size_t>> &adj, double s)
{
    const size_t N = vertices_.size() / 3;
    if (N == 0)
        return;

    std::vector<float> out(vertices_.size());

    for (size_t i = 0; i < N; ++i)
    {
        const float *vi = &vertices_[3 * i];

        double cx = 0.0, cy = 0.0, cz = 0.0;
        const auto &nb = adj[i];

        if (!nb.empty())
        {
            for (size_t j : nb)
            {
                const float *vj = &vertices_[3 * j];
                cx += vj[0];
                cy += vj[1];
                cz += vj[2];
            }
            const double inv = 1.0 / static_cast<double>(nb.size());
            cx *= inv;
            cy *= inv;
            cz *= inv;
        }
        else
        {
            cx = vi[0];
            cy = vi[1];
            cz = vi[2];
        }

        out[3 * i + 0] = static_cast<float>(vi[0] + s * (cx - vi[0]));
        out[3 * i + 1] = static_cast<float>(vi[1] + s * (cy - vi[1]));
        out[3 * i + 2] = static_cast<float>(vi[2] + s * (cz - vi[2]));
    }

    vertices_.swap(out);
}

void Mesh::taubinSmoothInPlace(int iterations, double lambda, double mu)
{
    if (iterations <= 0 || vertices_.empty())
        return;

    taubinSmoothInPlaceIte(iterations, lambda, mu, buildAdjacency());
}
void Mesh::taubinSmoothInPlaceIte(int iterations, double lambda, double mu, const std::vector<std::vector<size_t>> &adjacency)
{
    if (iterations <= 0 || vertices_.empty())
        return;

    for (int it = 0; it < iterations; ++it)
    {
        laplaceStepUniform_(adjacency, lambda);
        laplaceStepUniform_(adjacency, mu);
    }
}

// Robust cotangent at vertex k opposite edge (i,j):
// cot(∠ikj) = dot(vi - vk, vj - vk) / ||(vi - vk) x (vj - vk)||
inline double Mesh::cotangentAt_(const float *vi, const float *vj, const float *vk)
{
    const double u0 = static_cast<double>(vi[0]) - vk[0];
    const double u1 = static_cast<double>(vi[1]) - vk[1];
    const double u2 = static_cast<double>(vi[2]) - vk[2];

    const double v0 = static_cast<double>(vj[0]) - vk[0];
    const double v1 = static_cast<double>(vj[1]) - vk[1];
    const double v2 = static_cast<double>(vj[2]) - vk[2];

    const double dot = u0 * v0 + u1 * v1 + u2 * v2;
    const double cx0 = u1 * v2 - u2 * v1;
    const double cx1 = u2 * v0 - u0 * v2;
    const double cx2 = u0 * v1 - u1 * v0;
    const double n = std::sqrt(cx0 * cx0 + cx1 * cx1 + cx2 * cx2);
    const double eps = 1e-12;

    if (n < eps)
        return 0.0; // degenerate triangle -> no contribution
    return dot / n;
}

// Build symmetric cotangent weights adjacency.
// Each undirected edge (i,j) gets w_ij = 0.5 * (cot(alpha) + cot(beta)),
// where alpha and beta are angles opposite the edge in its incident triangles.
std::vector<std::vector<Mesh::NeighborW>> Mesh::buildCotanAdjacency_() const
{
    if (faces_.empty())
        throw std::runtime_error("buildCotanAdjacency_: requires triangle faces.");

    const size_t nV = vertices_.size() / 3;

    struct EdgeKey
    {
        size_t a, b; // a < b
        bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
    };
    struct EdgeKeyHash
    {
        std::size_t operator()(const EdgeKey &k) const noexcept
        {
            // Simple mix (you can replace with a better hash if desired)
            return std::hash<size_t>()(k.a * 1469598103934665603ULL ^ k.b);
        }
    };

    std::unordered_map<EdgeKey, double, EdgeKeyHash> wmap;
    wmap.reserve(faces_.size() * 3);

    auto add_half_cot = [&](size_t i, size_t j, size_t k)
    {
        // edge (i,j) with opposite vertex k contributes 0.5 * cot(angle at k)
        const float *vi = &vertices_[3 * i];
        const float *vj = &vertices_[3 * j];
        const float *vk = &vertices_[3 * k];
        const double c = 0.5 * cotangentAt_(vi, vj, vk);

        EdgeKey key{std::min(i, j), std::max(i, j)};
        auto it = wmap.find(key);
        if (it == wmap.end())
        {
            wmap.emplace(key, c);
        }
        else
        {
            it->second += c;
        }
    };

    // Accumulate 0.5*cot for each triangle corner on its opposite edge
    for (const auto &f : faces_)
    {
        const size_t a = f[0], b = f[1], c = f[2];
        add_half_cot(a, b, c); // opposite c
        add_half_cot(b, c, a); // opposite a
        add_half_cot(c, a, b); // opposite b
    }

    // Build adjacency with symmetric weights
    std::vector<std::vector<NeighborW>> adjW(nV);
    adjW.reserve(nV);

    for (const auto &kv : wmap)
    {
        const size_t i = kv.first.a;
        const size_t j = kv.first.b;
        const double w = kv.second;

        if (w <= 0.0)
            continue; // optional: drop non-positive weights

        adjW[i].push_back({j, w});
        adjW[j].push_back({i, w});
    }

    return adjW;
}

// One cotangent-weighted Laplacian step (normalized)
void Mesh::laplaceStepCotan_(const std::vector<std::vector<NeighborW>> &adjW, double s)
{
    const size_t N = vertices_.size() / 3;
    if (N == 0)
        return;

    std::vector<float> out(vertices_.size());

    for (size_t i = 0; i < N; ++i)
    {
        const float *vi = &vertices_[3 * i];
        const auto &nb = adjW[i];

        if (nb.empty())
        {
            // Isolated vertex: copy through
            out[3 * i + 0] = vi[0];
            out[3 * i + 1] = vi[1];
            out[3 * i + 2] = vi[2];
            continue;
        }

        double sx = 0.0, sy = 0.0, sz = 0.0, sw = 0.0;
        for (const auto &n : nb)
        {
            const float *vj = &vertices_[3 * n.j];
            sx += n.w * vj[0];
            sy += n.w * vj[1];
            sz += n.w * vj[2];
            sw += n.w;
        }

        if (sw <= 0.0)
        {
            // Fallback to copy (or you can do uniform)
            out[3 * i + 0] = vi[0];
            out[3 * i + 1] = vi[1];
            out[3 * i + 2] = vi[2];
            continue;
        }

        const double cx = sx / sw;
        const double cy = sy / sw;
        const double cz = sz / sw;

        out[3 * i + 0] = static_cast<float>(vi[0] + s * (cx - vi[0]));
        out[3 * i + 1] = static_cast<float>(vi[1] + s * (cy - vi[1]));
        out[3 * i + 2] = static_cast<float>(vi[2] + s * (cz - vi[2]));
    }

    vertices_.swap(out);
}

void Mesh::taubinSmoothCotanInPlace(int iterations, double lambda, double mu)
{
    if (iterations <= 0 || vertices_.empty())
        return;

    auto adjW = buildCotanAdjacency_();
    taubinSmoothCotanInPlaceIte(iterations, lambda, mu, adjW);
}
void Mesh::taubinSmoothCotanInPlaceIte(int iterations, double lambda, double mu, const std::vector<std::vector<NeighborW>> &adjW)
{
    if (iterations <= 0 || vertices_.empty())
        return;

    for (int it = 0; it < iterations; ++it)
    {
        laplaceStepCotan_(adjW, lambda);
        laplaceStepCotan_(adjW, mu);
    }
}

///////////////////////////////////////////
////////         NORMALIZE         ////////
///////////////////////////////////////////

UnitXform Mesh::computeUnitXform(bool center) const
{
    MinMaxData mm = computeMinMax();
    double r = static_cast<double>(mm.maxRange);
    if (r <= std::numeric_limits<double>::epsilon())
        return UnitXform{{0.0, 0.0, 0.0}, 1.0, center}; // identity

    std::array<double, 3> shift;
    if (center)
        shift = {0.5 * (mm.minXYZ[0] + mm.maxXYZ[0]), 0.5 * (mm.minXYZ[1] + mm.maxXYZ[1]), 0.5 * (mm.minXYZ[2] + mm.maxXYZ[2])};
    else
        shift = {mm.minXYZ[0], mm.minXYZ[1], mm.minXYZ[2]};

    double s = 2.0 / r; // scale to fit in [-1,1]
    return UnitXform{shift, s, center};
}

void Mesh::applyUnitInPlace(const UnitXform &xf)
{
    for (size_t i = 0; i + 2 < vertices_.size(); i += 3)
    {
        vertices_[i + 0] = static_cast<float>((vertices_[i + 0] - xf.shift[0]) * xf.scale);
        vertices_[i + 1] = static_cast<float>((vertices_[i + 1] - xf.shift[1]) * xf.scale);
        vertices_[i + 2] = static_cast<float>((vertices_[i + 2] - xf.shift[2]) * xf.scale);
    }
}

void Mesh::applyInverseUnitInPlace(const UnitXform &xf)
{
    // inverse of (v - shift) * s  is  v / s + shift
    const double invS = (xf.scale == 0.0) ? 1.0 : 1.0 / xf.scale; // equals r/2 when s = 2/r
    for (size_t i = 0; i + 2 < vertices_.size(); i += 3)
    {
        vertices_[i + 0] = static_cast<float>(vertices_[i + 0] * invS + xf.shift[0]);
        vertices_[i + 1] = static_cast<float>(vertices_[i + 1] * invS + xf.shift[1]);
        vertices_[i + 2] = static_cast<float>(vertices_[i + 2] * invS + xf.shift[2]);
    }
}

///////////////////////////////////////////
////////        EDGE LENGTH        ////////
///////////////////////////////////////////

std::vector<double> Mesh::edgeLengths() const
{
    std::set<std::pair<size_t, size_t>> seen;
    std::vector<double> out;
    for (auto &f : faces_)
    {
        for (int e = 0; e < 3; ++e)
        {
            size_t u = f[e], v = f[(e + 1) % 3];
            if (u > v)
                std::swap(u, v);
            if (seen.insert({u, v}).second)
            {
                size_t ui = 3 * u, vi = 3 * v;
                double dx = vertices_[vi] - vertices_[ui];
                double dy = vertices_[vi + 1] - vertices_[ui + 1];
                double dz = vertices_[vi + 2] - vertices_[ui + 2];
                out.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
    }
    return out;
}

///////////////////////////////////////////
////////          METRICS          ////////
///////////////////////////////////////////

MinMaxData Mesh::computeMinMax() const
{
    MinMaxData mm;
    if (vertices_.empty())
    {
        mm.minXYZ = {0.0f, 0.0f, 0.0f};
        mm.maxXYZ = {0.0f, 0.0f, 0.0f};
        mm.globalMin = mm.globalMax = mm.minRange = mm.maxRange = 0.0f;
        return mm;
    }
    // initialize to the first vertex
    mm.minXYZ = mm.maxXYZ = {vertices_[0], vertices_[1], vertices_[2]};

    // scan all vertices
    const size_t Nf = vertices_.size();
    for (size_t i = 0; i + 2 < Nf; i += 3)
    {
        for (int c = 0; c < 3; ++c)
        {
            mm.minXYZ[c] = std::min(mm.minXYZ[c], vertices_[i + c]);
            mm.maxXYZ[c] = std::max(mm.maxXYZ[c], vertices_[i + c]);
        }
    }

    // per‐axis ranges
    float rx = mm.maxXYZ[0] - mm.minXYZ[0];
    float ry = mm.maxXYZ[1] - mm.minXYZ[1];
    float rz = mm.maxXYZ[2] - mm.minXYZ[2];
    mm.minRange = std::min({rx, ry, rz});
    mm.maxRange = std::max({rx, ry, rz});

    // global min/max
    mm.globalMin = std::min({mm.minXYZ[0], mm.minXYZ[1], mm.minXYZ[2]});
    mm.globalMax = std::max({mm.maxXYZ[0], mm.maxXYZ[1], mm.maxXYZ[2]});
    return mm;
}

double Mesh::rmse(const Mesh &o) const
{
    const auto &A = vertices_, &B = o.vertices_;
    if (A.size() != B.size())
        throw std::runtime_error("rmse: size mismatch");

    size_t M = A.size() / 3;
    double sumSq = 0.0;
    for (size_t i = 0; i < M; ++i)
    {
        size_t k = 3 * i;
        double dx = static_cast<double>(A[k]) - static_cast<double>(B[k]);
        double dy = static_cast<double>(A[k + 1]) - static_cast<double>(B[k + 1]);
        double dz = static_cast<double>(A[k + 2]) - static_cast<double>(B[k + 2]);
        sumSq += dx * dx + dy * dy + dz * dz;
    }
    return std::sqrt(sumSq / static_cast<double>(M));
}
double Mesh::hausdorff(const Mesh &o) const
{
    using KDTreeType = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, Mesh>, Mesh, 3>;
    KDTreeType treeA(3, *this, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    KDTreeType treeB(3, o, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    treeA.buildIndex();
    treeB.buildIndex();

    auto directed = [&](const Mesh &src, KDTreeType &dstTree)
    {
        double worstSq = 0.0;
        size_t P = src.kdtree_get_point_count();
        for (size_t i = 0; i < P; ++i)
        {
            float qpt[3] = {src.kdtree_get_pt(i, 0), src.kdtree_get_pt(i, 1), src.kdtree_get_pt(i, 2)};
            size_t outIndex;
            double outDistSq;
            nanoflann::KNNResultSet<double> resultSet(1);
            resultSet.init(&outIndex, &outDistSq);
            dstTree.findNeighbors(resultSet, qpt, {});
            worstSq = std::max(worstSq, outDistSq);
        }
        return worstSq;
    };

    double aToB = directed(*this, treeB);
    double bToA = directed(o, treeA);
    return std::sqrt(std::max(aToB, bToA));
}
double Mesh::npcr(const Mesh &o) const
{
    const auto &A = vertices_, &B = o.vertices_;
    if (A.size() != B.size())
        throw std::runtime_error("npcr: size mismatch");

    size_t N = A.size();
    size_t diffCount = 0;
    for (size_t i = 0; i < N; ++i)
        if (!floatEqual(A[i], B[i]))
            ++diffCount;

    return (static_cast<double>(diffCount) / static_cast<double>(N)) * 100.0;
}
double Mesh::uaci(const Mesh &o) const
{
    auto mmA = computeMinMax();
    auto mmB = o.computeMinMax();
    constexpr int L = 8;
    const int levels = (1 << L) - 1; // 255

    // Helper lambdas to compute 1/(span) with guard
    auto invRangeA = [&](int c)
    {
        float span = mmA.maxXYZ[c] - mmA.minXYZ[c];
        return (span > 0.0f) ? (levels / static_cast<double>(span)) : 0.0;
    };
    auto invRangeB = [&](int c)
    {
        float span = mmB.maxXYZ[c] - mmB.minXYZ[c];
        return (span > 0.0f) ? (levels / static_cast<double>(span)) : 0.0;
    };

    double iAx = invRangeA(0), iAy = invRangeA(1), iAz = invRangeA(2);
    double iBx = invRangeB(0), iBy = invRangeB(1), iBz = invRangeB(2);

    size_t M = vertices_.size() / 3;
    double sumNorm = 0.0;

    for (size_t i = 0; i < M; ++i)
    {
        size_t k = 3 * i;
        double qAx = std::floor((vertices_[k] - mmA.minXYZ[0]) * iAx);
        double qAy = std::floor((vertices_[k + 1] - mmA.minXYZ[1]) * iAy);
        double qAz = std::floor((vertices_[k + 2] - mmA.minXYZ[2]) * iAz);

        double qBx = std::floor((o.vertices_[k] - mmB.minXYZ[0]) * iBx);
        double qBy = std::floor((o.vertices_[k + 1] - mmB.minXYZ[1]) * iBy);
        double qBz = std::floor((o.vertices_[k + 2] - mmB.minXYZ[2]) * iBz);

        sumNorm += (std::abs(qAx - qBx) + std::abs(qAy - qBy) + std::abs(qAz - qBz)) / static_cast<double>(levels);
    }
    // Divide by (M * 3) then *100
    return (sumNorm / (static_cast<double>(M) * 3.0)) * 100.0;
}
double Mesh::entropy() const
{
    size_t M = vertices_.size() / 3;
    if (M == 0)
        return 0.0;

    auto mm = computeMinMax();
    constexpr int L = 8;

    if (mm.minRange <= 0.0f)
        return 0.0; // If all points lie in a lower‐dimensional subspace → zero entropy.

    double binWidth = mm.minRange / static_cast<double>(L);

    // Construct L×L×L histogram
    std::vector<std::vector<std::vector<int>>> hist(
        L, std::vector<std::vector<int>>(L, std::vector<int>(L, 0)));

    for (size_t i = 0; i < M; ++i)
    {
        size_t k = 3 * i;
        int ix = static_cast<int>(std::floor((vertices_[k] - mm.minXYZ[0]) / binWidth));
        int iy = static_cast<int>(std::floor((vertices_[k + 1] - mm.minXYZ[1]) / binWidth));
        int iz = static_cast<int>(std::floor((vertices_[k + 2] - mm.minXYZ[2]) / binWidth));

        ix = std::clamp(ix, 0, L - 1);
        iy = std::clamp(iy, 0, L - 1);
        iz = std::clamp(iz, 0, L - 1);
        hist[ix][iy][iz]++;
    }

    double H = 0.0;
    for (int x = 0; x < L; ++x)
    {
        for (int y = 0; y < L; ++y)
        {
            for (int z = 0; z < L; ++z)
            {
                int cnt = hist[x][y][z];
                if (cnt == 0)
                    continue;
                double p = static_cast<double>(cnt) / static_cast<double>(M);
                H -= p * std::log2(p);
            }
        }
    }
    return H;
}
std::array<double, 4> Mesh::pearsonCorrelation(const Mesh &o) const
{
    if (o.vertices_.size() != vertices_.size())
        throw std::runtime_error("pearsonCorrelation: size mismatch");
    const size_t M = vertices_.size() / 3;
    if (M == 0)
        throw std::runtime_error("pearsonCorrelation: empty mesh");

    double sumA[3] = {0.0, 0.0, 0.0};
    double sumB[3] = {0.0, 0.0, 0.0};
    double sumA2[3] = {0.0, 0.0, 0.0};
    double sumB2[3] = {0.0, 0.0, 0.0};
    double sumAB[3] = {0.0, 0.0, 0.0};

    for (size_t i = 0; i < M; ++i)
    {
        const size_t k = 3 * i;
        for (int c = 0; c < 3; ++c)
        {
            const double a = vertices_[k + c];
            const double b = o.vertices_[k + c];
            sumA[c] += a;
            sumB[c] += b;
            sumA2[c] += a * a;
            sumB2[c] += b * b;
            sumAB[c] += a * b;
        }
    }

    std::array<double, 4> r{};
    double avg = 0.0;
    const double n = static_cast<double>(M);

    for (int c = 0; c < 3; ++c)
    {
        const double meanA = sumA[c] / n, meanB = sumB[c] / n;
        const double cov = (sumAB[c] / n) - meanA * meanB;
        const double varA = (sumA2[c] / n) - meanA * meanA;
        const double varB = (sumB2[c] / n) - meanB * meanB;

        double rc = 0.0;
        if (varA > 0.0 && varB > 0.0)
        {
            rc = cov / std::sqrt(varA * varB);
            if (rc > 1.0)
                rc = 1.0;
            if (rc < -1.0)
                rc = -1.0;
        }
        r[c] = rc;
        avg += rc;
    }
    r[3] = avg / 3.0;
    return r; // [0]=X, [1]=Y, [2]=Z, [3]=mean(X,Y,Z)
}
std::array<double, 4> Mesh::autoCorrelationLag1Index() const
{
    const size_t M = vertices_.size() / 3;
    if (M < 2)
        throw std::runtime_error("autoCorrelationLag1: at least 2 vertices required");

    const size_t N = M - 1;

    double sumA[3] = {0.0, 0.0, 0.0};
    double sumB[3] = {0.0, 0.0, 0.0};
    double sumA2[3] = {0.0, 0.0, 0.0};
    double sumB2[3] = {0.0, 0.0, 0.0};
    double sumAB[3] = {0.0, 0.0, 0.0};

    for (size_t i = 0; i + 1 < M; ++i)
    {
        const size_t k0 = 3 * i;
        const size_t k1 = 3 * (i + 1);
        for (int c = 0; c < 3; ++c)
        {
            const double a = vertices_[k0 + c];
            const double b = vertices_[k1 + c];
            sumA[c] += a;
            sumB[c] += b;
            sumA2[c] += a * a;
            sumB2[c] += b * b;
            sumAB[c] += a * b;
        }
    }

    std::array<double, 4> r{};
    double avg = 0.0;
    const double n = static_cast<double>(N);

    for (int c = 0; c < 3; ++c)
    {
        const double meanA = sumA[c] / n, meanB = sumB[c] / n;
        const double cov = (sumAB[c] / n) - meanA * meanB;
        const double varA = (sumA2[c] / n) - meanA * meanA;
        const double varB = (sumB2[c] / n) - meanB * meanB;

        double rc = 0.0;
        if (varA > 0.0 && varB > 0.0)
        {
            rc = cov / std::sqrt(varA * varB);
            if (rc > 1.0)
                rc = 1.0;
            if (rc < -1.0)
                rc = -1.0;
        }
        r[c] = rc;
        avg += rc;
    }
    r[3] = avg / 3.0;
    return r; // [0]=X, [1]=Y, [2]=Z, [3]=mean(X,Y,Z)
}

double Mesh::autoCorrelationLag1Seed(size_t seedIdx) const
{
    const size_t M = vertices_.size() / 3;
    if (M < 2 || seedIdx >= M)
        return 0.0;

    // Euclidean radii to seed
    const double sx = vertices_[3 * seedIdx + 0];
    const double sy = vertices_[3 * seedIdx + 1];
    const double sz = vertices_[3 * seedIdx + 2];

    std::vector<double> r(M);
    for (size_t i = 0; i < M; ++i)
    {
        const double dx = vertices_[3 * i + 0] - sx;
        const double dy = vertices_[3 * i + 1] - sy;
        const double dz = vertices_[3 * i + 2] - sz;
        r[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // lag-1 Pearson on current index order
    double mean = 0.0;
    for (size_t i = 0; i + 1 < M; ++i)
        mean += r[i];
    mean /= double(M - 1);

    double num = 0.0, den1 = 0.0, den2 = 0.0;
    for (size_t i = 0; i + 1 < M; ++i)
    {
        const double a = r[i] - mean;
        const double b = r[i + 1] - mean;
        num += a * b;
        den1 += a * a;
        den2 += b * b;
    }

    if (den1 <= 0.0 || den2 <= 0.0)
        return 0.0;
    return num / std::sqrt(den1 * den2);
}

///////////////////////////////////////
////////       GNUPLOT        /////////
///////////////////////////////////////

void initGnuplotParamAndStyle(Gnuplot &gp)
{
    // OUTPUT
    gp << "set terminal pdf enhanced size 20cm, 15cm" << std::endl; // Output Format
    gp << "set key outside" << std::endl;                           // Legends position
    // gp << "set format x '%.1e'" << std::endl;                    // 3 digit precision

    // SEABORN STYLE
    gp << "set object rectangle from graph 0,0 to graph 1,1 behind fillcolor rgb '#ececec' fillstyle solid noborder" << std::endl; // Lightgrey background
    gp << "set grid xtics ytics lc rgb 'white' lw 1 lt 1" << std::endl;                                                            // White grid
    gp << "set xtics scale 0" << std::endl;                                                                                        // Remove X axis tics
    gp << "set ytics scale 0" << std::endl;                                                                                        // Remove Y axis tics
    gp << "set border 0 lc rgb 'black'" << std::endl;                                                                              // Remove black border

    // Set offsets to ensure minimal offset with the plot borders <left>, <right>, <top>, <bottom>
    // gp << "set offset graph 0.01, graph 0.01, graph 0, graph 0" << std::endl;

    // PALETTE VIRIDIS
    gp << "set palette defined ( 0 '#440154', 1 '#472c7a', 2 '#3b518b', 3 '#2c718e', 4 '#21908d', 5 '#27ad81', 6 '#5cc863', 7 '#aadc32', 8 '#fde725' )" << std::endl;
    gp << "unset colorbox" << std::endl; // Hide palette on plot

    // OUTLIERS BOXPLOT
    gp << "set style boxplot nooutliers" << std::endl;
}

void Mesh::plotHistPoints(Gnuplot &gp, const std::string &out, int bins) const
{
    MinMaxData mm = computeMinMax();
    size_t M = vertices_.size() / 3;
    std::vector<float> X(M), Y(M), Z(M);
    for (size_t i = 0; i < M; ++i)
    {
        X[i] = vertices_[3 * i];
        Y[i] = vertices_[3 * i + 1];
        Z[i] = vertices_[3 * i + 2];
    }
    float w = mm.maxRange / bins;

    // INIT
    gp << "set output '" << out << ".pdf'" << std::endl;
    gp << "set ylabel 'Occurrences'" << std::endl;
    gp << "set xlabel 'Values'" << std::endl;
    gp << "set xrange [" << mm.globalMin << ":" << mm.globalMax << "]" << std::endl;
    gp << "set boxwidth " << w << " absolute" << std::endl;
    gp << "set key inside right top box opaque" << std::endl;

    // Plot all histograms in a single plot
    gp << "plot '-' using (floor($1 / " << w << ") * " << w << " + " << w << "/2.0):(1.0) smooth freq with boxes fs solid border lc rgb 'black' lt rgb '0x220154' title 'X axis',"
       << "'-' using (floor($1 / " << w << ") * " << w << " + " << w << "/2.0):(1.0) smooth freq with boxes fs solid border lc rgb 'black' lt rgb '0x223b518b' title 'Y axis',"
       << "'-' using (floor($1 / " << w << ") * " << w << " + " << w << "/2.0):(1.0) smooth freq with boxes fs solid border lc rgb 'black' lt rgb '0x2221908d' title 'Z axis'" << std::endl;

    gp.send1d(X);
    gp.send1d(Y);
    gp.send1d(Z);

    // UNSET
    gp << "unset output" << std::endl;
    gp << "unset xrange" << std::endl;
    gp << "unset xlabel" << std::endl;
    gp << "unset ylabel" << std::endl;
    gp << "unset boxwidth" << std::endl;
    gp << "unset style fill" << std::endl;
    gp << "unset key" << std::endl;
}

void Mesh::plotHistEdges(Gnuplot &gp, const std::string &out, int bins) const
{
    std::vector<double> E = this->edgeLengths();
    float mn = *std::min_element(E.begin(), E.end());
    float mx = *std::max_element(E.begin(), E.end());
    float w = (mx - mn) / bins;

    // INIT
    gp << "set output '" << out << ".pdf'" << std::endl;
    gp << "set ylabel 'Occurences'" << std::endl;
    gp << "set xlabel 'Edge Length'" << std::endl;
    gp << "set boxwidth " << w << " absolute" << std::endl;
    gp << "plot '-' using (floor($1 / " << w << ") * " << w << " + " << w << "/2.0):(1.0) smooth freq with boxes fs solid border lc rgb 'black' lt palette frac 0.0 notitle" << std::endl;
    gp.send1d(E);

    // UNSET
    gp << "unset output" << std::endl;
    gp << "unset boxwidth" << std::endl;
    gp << "unset xlabel" << std::endl;
    gp << "unset ylabel" << std::endl;
}

void Mesh::plotPointCorrIndex(Gnuplot &gp, const std::string &out) const
{
    size_t M = vertices_.size() / 3;
    if (M < 2)
        return;

    std::vector<std::pair<float, float>> CX(M - 1), CY(M - 1), CZ(M - 1);
    for (size_t i = 0; i + 1 < M; ++i)
    {
        size_t idx0 = 3 * i;
        size_t idx1 = 3 * (i + 1);
        CX[i] = {vertices_[idx0], vertices_[idx1]};
        CY[i] = {vertices_[idx0 + 1], vertices_[idx1 + 1]};
        CZ[i] = {vertices_[idx0 + 2], vertices_[idx1 + 2]};
    }
    MinMaxData mm = computeMinMax();

    // INIT (Save in png otherwise it explode...)
    gp << "set terminal jpeg size 1500, 500 enhanced" << std::endl;
    gp << "set output '" << out << ".jpg'" << std::endl;
    gp << "set xrange [" << mm.globalMin << ":" << mm.globalMax << "]" << std::endl;
    gp << "set yrange [" << mm.globalMin << ":" << mm.globalMax << "]" << std::endl;
    gp << "set size square" << std::endl;
    gp << "set multiplot layout 1,3 columnsfirst" << std::endl;

    // Define axis labels and colors for each axis
    std::vector<std::string> labelsX = {"X", "Y", "Z"};
    std::vector<std::string> labelsY = {"X+1", "Y+1", "Z+1"};
    std::vector<std::string> colors = {"0xDD440154", "0xDD3b518b", "0xDD21908d"};
    std::vector<std::vector<std::pair<float, float>>> coords = {CX, CY, CZ};

    // Plot correlations for each axis
    for (size_t i = 0; i < coords.size(); ++i)
    {
        gp << "set xlabel '" << labelsX[i] << "'\n";
        gp << "set ylabel '" << labelsY[i] << "'\n";
        gp << "plot '-' using ($1):($2) with points pt 7 ps 0.25 lc rgb " << colors[i] << " notitle" << std::endl;
        gp.send1d(coords[i]);
    }

    // UNSET
    gp << "unset multiplot" << std::endl;
    gp << "set size nosquare" << std::endl;
    gp << "unset xrange" << std::endl;
    gp << "unset yrange" << std::endl;
    gp << "unset xlabel" << std::endl;
    gp << "unset ylabel" << std::endl;
    gp << "unset output" << std::endl;
    gp << "set terminal pdf enhanced size 20cm, 15cm" << std::endl;
}

void Mesh::plotPointCorrSeed(Gnuplot &gp, const std::string &out, size_t seedIdx) const
{
    const size_t M = vertices_.size() / 3;
    if (M < 2 || seedIdx >= M)
        return;

    // Euclidean radii from seed
    const double sx = vertices_[3 * seedIdx + 0];
    const double sy = vertices_[3 * seedIdx + 1];
    const double sz = vertices_[3 * seedIdx + 2];
    std::vector<double> r(M);
    for (size_t i = 0; i < M; ++i)
    {
        const double dx = vertices_[3 * i + 0] - sx;
        const double dy = vertices_[3 * i + 1] - sy;
        const double dz = vertices_[3 * i + 2] - sz;
        r[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Build (r[i], r[i+1]) pairs
    std::vector<std::pair<double, double>> RR;
    RR.reserve(M - 1);
    double rmin = std::numeric_limits<double>::infinity();
    double rmax = -rmin;
    for (size_t i = 0; i + 1 < M; ++i)
    {
        RR.emplace_back(r[i], r[i + 1]);
        rmin = std::min({rmin, r[i], r[i + 1]});
        rmax = std::max({rmax, r[i], r[i + 1]});
    }
    if (!(rmin < rmax))
    {
        rmin = 0.0;
        rmax = 1.0;
    }

    // Render
    gp << "set terminal jpeg size 800, 800 enhanced\n";
    gp << "set output '" << out << ".jpg'\n";
    gp << "set size square\n";
    gp << "set xlabel 'r_i = ||v_i - seed||'\n";
    gp << "set ylabel 'r_{i+1} = ||v_{i+1} - seed||'\n";
    gp << "set xrange [" << rmin << ":" << rmax << "]\n";
    gp << "set yrange [" << rmin << ":" << rmax << "]\n";
    gp << "plot '-' using 1:2 with points pt 7 ps 0.25 lc rgb 0xDD3b518b notitle\n";
    gp.send1d(RR);

    // UNSET
    gp << "unset output" << std::endl;
    gp << "set size nosquare" << std::endl;
    gp << "unset xlabel" << std::endl;
    gp << "unset ylabel" << std::endl;
    gp << "unset xrange" << std::endl;
    gp << "unset yrange" << std::endl;
    gp << "set terminal pdf enhanced size 20cm, 15cm" << std::endl;
}

///////////////////////////////////////////
////////         NANOFLANN         ////////
///////////////////////////////////////////

template <class BBOX>
bool Mesh::kdtree_get_bbox(BBOX &bb) const
{
    auto mm = computeMinMax();
    for (int i = 0; i < 3; ++i)
    {
        bb[i].low = mm.minXYZ[i];
        bb[i].high = mm.maxXYZ[i];
    }
    return true;
}
size_t Mesh::kdtree_get_point_count() const { return vertices_.size() / 3; }
float Mesh::kdtree_get_pt(size_t i, size_t d) const { return vertices_[3 * i + d]; }

///////////////////////////////////////////
////////   SELECTIVE  ENCRYPTION   ////////
///////////////////////////////////////////

void Mesh::selectEncrAESCFB(int nbBitsEncr, int nbBitsOffset, bool isEncryption, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv)
{
    computeSolo_AES_CFB(this->vertices_.data(), this->vertices_.size(), key, iv, nbBitsEncr, nbBitsOffset, isEncryption);
}

void Mesh::fullEncrLorenz3D(bool isEncryption, const CryptoPP::SecByteBlock &key, const CryptoPP::SecByteBlock &iv)
{
    computeSolo_LorenzAffine(this->vertices_.data(), this->vertices_.size(), key, iv, isEncryption);
}

void Mesh::processBits(uint8_t *const streamP, uint8_t *const streamQ, uint8_t *const streamR, const size_t p, const size_t q, const size_t r, const bool isExtract, const bool pEnabled, const bool qEnabled, const bool rEnabled)
{
    processMSB_PQR(this->vertices_.data(), this->vertices_.size(), streamP, streamQ, streamR, p, q, r, isExtract, pEnabled, qEnabled, rEnabled);
}
