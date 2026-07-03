#pragma once
#include <ostream>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <utility>
#include <sstream>
#include <iomanip>
#include <optional>
#include <type_traits>
#include <cmath>
#include <limits>

namespace csv
{
    struct SmartFloatFormat
    {
        int significant = 6;  // significant digits (good default for analysis)
        int sci_low_exp = -5; // use scientific if exp10 < -5
        int sci_high_exp = 8; // or exp10 >= 8
        bool trim_trailing_zeros = true;
    };

    inline void trim_trailing_zeros_inplace(std::string &s)
    {
        // Trim trailing zeros in the mantissa (works for both defaultfloat and scientific)
        auto pos_e = s.find_first_of("eE");
        auto trim_one = [](std::string &t)
        {
            auto dot = t.find('.');
            if (dot == std::string::npos)
                return;
            size_t end = t.size();
            while (end > dot + 1 && t[end - 1] == '0')
                --end;
            if (end == dot + 1)
                --end; // remove the dot if nothing after it
            t.erase(end);
        };

        if (pos_e == std::string::npos)
        {
            trim_one(s);
        }
        else
        {
            std::string mant = s.substr(0, pos_e);
            std::string exp = s.substr(pos_e); // keep exponent as is
            trim_one(mant);
            s = mant + exp;
        }
    }

    inline std::string format_float(double v, const SmartFloatFormat &f = {})
    {
        if (std::isnan(v))
            return "nan";
        if (std::isinf(v))
            return (v > 0 ? "inf" : "-inf");
        if (v == 0.0)
            return "0";

        double av = std::fabs(v);
        // exp10 = floor(log10(|v|))
        int exp10 = static_cast<int>(std::floor(std::log10(av)));

        std::ostringstream oss;
        // Use defaultfloat with N significant digits in the "normal" range,
        // otherwise scientific with (N-1) digits after the first.
        if (exp10 < f.sci_low_exp || exp10 >= f.sci_high_exp)
        {
            oss << std::scientific << std::setprecision(std::max(1, f.significant - 1)) << v;
        }
        else
        {
            oss << std::defaultfloat << std::setprecision(std::max(1, f.significant)) << v;
        }

        std::string s = oss.str();
        if (f.trim_trailing_zeros)
            trim_trailing_zeros_inplace(s);
        return s;
    }

    // ------------------------
    // Options
    // ------------------------
    struct Options
    {
        char sep = ',';
        char quote = '"';
        std::string newline = "\n";
        bool always_quote = false;
        bool write_utf8_bom = false;
        bool strict_size = false;               // vector rows must match header size
        bool strict_names = true;               // map rows must match header names (no missing/extra)
        bool auto_header_from_first_row = true; // infer header from first row's keys

        enum class HeaderOrder
        {
            PreserveListOrder,
            Alphabetical
        };
        HeaderOrder header_order = HeaderOrder::PreserveListOrder;
    };

    // ------------------------
    // Helpers
    // ------------------------
    inline void write_bom_if_needed(std::ostream &os, const Options &opt)
    {
        if (opt.write_utf8_bom)
        {
            // UTF-8 BOM
            os.put(static_cast<char>(0xEF));
            os.put(static_cast<char>(0xBB));
            os.put(static_cast<char>(0xBF));
        }
    }

    inline void write_cell(std::ostream &os, std::string_view cell, const Options &opt)
    {
        bool needs_quote = opt.always_quote;
        for (char c : cell)
        {
            if (c == opt.sep || c == opt.quote || c == '\n' || c == '\r')
            {
                needs_quote = true;
                break;
            }
        }

        if (!needs_quote)
        {
            os.write(cell.data(), static_cast<std::streamsize>(cell.size()));
            return;
        }

        os.put(opt.quote);
        for (char c : cell)
        {
            if (c == opt.quote)
                os.put(opt.quote); // escape by doubling
            os.put(c);
        }
        os.put(opt.quote);
    }

    template <class T>
    inline std::string to_string_generic(const T &v)
    {
        if constexpr (std::is_same_v<T, std::string>)
        {
            return v;
        }
        else if constexpr (std::is_convertible_v<T, std::string>)
        {
            return static_cast<std::string>(v);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            return format_float(static_cast<double>(v)); // smart general/scientific formatting
        }
        else if constexpr (std::is_integral_v<T>)
        {
            std::ostringstream oss;
            oss << v; // integers keep plain formatting
            return oss.str();
        }
        else
        {
            static_assert(sizeof(T) == 0, "to_string_generic: provide a lambda to convert this type to std::string");
        }
    }

    inline std::string to_string_optional_empty(std::optional<std::string> v)
    {
        return v.has_value() ? *v : std::string{};
    }

    inline std::string to_string_fixed(double v, int precision = 6)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << v;
        return oss.str();
    }

    // ------------------------
    // Dynamic CSV writer
    // ------------------------

    class WriterDynamic
    {
    public:
        WriterDynamic(std::ostream &os, std::vector<std::string> header, Options opt = {})
            : os_(os), header_(std::move(header)), opt_(opt) {}

        void writeHeader()
        {
            if (header_written_)
                return;
            write_bom_if_needed(os_, opt_);
            write_row_cells(header_);
            header_written_ = true;
        }

        void writeRow(const std::vector<std::pair<std::string, std::string>> &kvs)
        {
            // infer header from first row if requested and not set
            if (!header_written_ && header_.empty() && opt_.auto_header_from_first_row)
            {
                header_.reserve(kvs.size());
                for (const auto &kv : kvs)
                    header_.push_back(kv.first); // preserve typed order
                writeHeader();                   // seal schema
            }

            // Build a fast lookup from the passed pairs
            std::unordered_map<std::string, std::string> byName;
            byName.reserve(kvs.size());
            for (const auto &kv : kvs)
                byName.emplace(kv.first, kv.second);

            // Strict name check (exact match)
            if (opt_.strict_names)
            {
                std::vector<std::string> missing, extra;
                // check missing
                for (const auto &col : header_)
                    if (!byName.count(col))
                        missing.push_back(col);
                // check extra
                for (const auto &kv : kvs)
                {
                    bool inHeader = false;
                    for (const auto &h : header_)
                        if (h == kv.first)
                        {
                            inHeader = true;
                            break;
                        }
                    if (!inHeader)
                        extra.push_back(kv.first);
                }
                if (!missing.empty() || !extra.empty())
                {
                    std::ostringstream msg;
                    msg << "csv::WriterDynamic: header/name mismatch.";
                    if (!missing.empty())
                    {
                        msg << " Missing: [";
                        for (size_t i = 0; i < missing.size(); ++i)
                        {
                            if (i)
                                msg << ",";
                            msg << missing[i];
                        }
                        msg << "]";
                    }
                    if (!extra.empty())
                    {
                        msg << " Extra: [";
                        for (size_t i = 0; i < extra.size(); ++i)
                        {
                            if (i)
                                msg << ",";
                            msg << extra[i];
                        }
                        msg << "]";
                    }
                    throw std::runtime_error(msg.str());
                }
            }

            // Emit in header order
            rowbuf_.clear();
            rowbuf_.reserve(header_.size());
            for (const auto &col : header_)
            {
                auto it = byName.find(col);
                rowbuf_.push_back(it == byName.end() ? std::string{} : it->second);
            }
            write_row_cells(rowbuf_);
        }

        // NEW: infer header from the first initializer_list row (preserves list order)
        void writeRow(std::initializer_list<std::pair<std::string, std::string>> kvs)
        {
            if (!header_written_ && header_.empty() && opt_.auto_header_from_first_row)
            {
                header_.reserve(kvs.size());
                for (const auto &kv : kvs)
                    header_.push_back(kv.first); // preserves typed order
                writeHeader();                   // seals the schema
            }

            // Build a byName map and delegate to the map overload
            std::unordered_map<std::string, std::string> byName;
            byName.reserve(kvs.size());
            for (const auto &kv : kvs)
                byName.emplace(kv.first, kv.second);
            writeRow(byName); // respects strict_names
        }

        // STRICT: size must match when strict_size=true
        void writeRow(const std::vector<std::string> &cells)
        {
            ensure_header();
            if (opt_.strict_size && cells.size() != header_.size())
            {
                throw std::runtime_error("csv::WriterDynamic: row size (" + std::to_string(cells.size()) +
                                         ") != header size (" + std::to_string(header_.size()) + ")");
            }

            rowbuf_.clear();
            if (opt_.strict_size)
            {
                rowbuf_ = cells; // exact
            }
            else
            {
                // fallback: pad/truncate
                rowbuf_.reserve(header_.size());
                for (size_t i = 0; i < header_.size(); ++i)
                {
                    rowbuf_.push_back(i < cells.size() ? cells[i] : std::string{});
                }
            }
            write_row_cells(rowbuf_);
        }

        // STRICT: names must match exactly when strict_names=true
        void writeRow(const std::unordered_map<std::string, std::string> &byName)
        {
            ensure_header();

            if (opt_.strict_names)
            {
                // compute missing & extra
                std::vector<std::string> missing, extra;
                // build set for byName
                std::unordered_map<std::string, bool> present;
                present.reserve(byName.size());
                for (const auto &kv : byName)
                    present.emplace(kv.first, true);

                // missing
                for (const auto &col : header_)
                {
                    if (!present.count(col))
                        missing.push_back(col);
                }
                // extra
                for (const auto &kv : byName)
                {
                    // check against header
                    bool inHeader = false;
                    // (small headers: linear scan is fine; optimize with unordered_set if huge)
                    for (const auto &h : header_)
                    {
                        if (h == kv.first)
                        {
                            inHeader = true;
                            break;
                        }
                    }
                    if (!inHeader)
                        extra.push_back(kv.first);
                }
                if (!missing.empty() || !extra.empty())
                {
                    std::ostringstream msg;
                    msg << "csv::WriterDynamic: header/name mismatch.";
                    if (!missing.empty())
                    {
                        msg << " Missing: [";
                        for (size_t i = 0; i < missing.size(); ++i)
                        {
                            if (i)
                                msg << ",";
                            msg << missing[i];
                        }
                        msg << "]";
                    }
                    if (!extra.empty())
                    {
                        msg << " Extra: [";
                        for (size_t i = 0; i < extra.size(); ++i)
                        {
                            if (i)
                                msg << ",";
                            msg << extra[i];
                        }
                        msg << "]";
                    }
                    throw std::runtime_error(msg.str());
                }
            }

            rowbuf_.clear();
            rowbuf_.reserve(header_.size());
            for (const auto &col : header_)
            {
                auto it = byName.find(col);
                rowbuf_.push_back(it == byName.end() ? std::string{} : it->second);
            }
            write_row_cells(rowbuf_);
        }

        // Read-only inspection helpers (no mutable access to header)
        size_t columnCount() const noexcept { return header_.size(); }
        std::string_view columnName(size_t i) const { return header_.at(i); }

    private:
        void ensure_header()
        {
            if (!header_written_)
                writeHeader();
        }
        void write_row_cells(const std::vector<std::string> &cells)
        {
            for (size_t i = 0; i < cells.size(); ++i)
            {
                if (i)
                    os_.put(opt_.sep);
                write_cell(os_, cells[i], opt_);
            }
            os_ << opt_.newline;
        }

        std::ostream &os_;
        std::vector<std::string> header_;
        Options opt_;
        bool header_written_ = false;
        std::vector<std::string> rowbuf_;
    };

    // ------------------------
    // Mapped CSV writer (typed)
    // ------------------------

    template <class T>
    class WriterMapped
    {
    public:
        using Getter = std::function<std::string(const T &)>;

        explicit WriterMapped(std::ostream &os, Options opt = {})
            : os_(os), opt_(opt) {}

        // NEW: prevent adding columns once schema is sealed (header written or first row emitted)
        WriterMapped &add(std::string name, Getter getter)
        {
            if (header_written_)
            {
                throw std::logic_error("csv::WriterMapped: cannot add columns after header has been written or a row has been emitted");
            }
            columns_.emplace_back(std::move(name), std::move(getter));
            return *this;
        }

        template <class NumGetter, class = std::enable_if_t<!std::is_same_v<std::decay_t<NumGetter>, Getter>>>
        WriterMapped &addNumber(std::string name, NumGetter g, int precision = -1, bool scientific = false)
        {
            Getter wrap = [g, precision, scientific](const T &t) -> std::string
            {
                if (precision < 0 && !scientific)
                {
                    // No explicit policy? Use smart float formatting.
                    return format_float(static_cast<double>(g(t)));
                }
                std::ostringstream oss;
                if (scientific)
                    oss << std::scientific;
                if (precision >= 0)
                    oss << std::setprecision(precision);
                oss << g(t);
                return oss.str();
            };

            return add(std::move(name), std::move(wrap));
        }

        template <class OptGetter>
        WriterMapped &addOptional(std::string name, OptGetter g)
        {
            Getter wrap = [g](const T &t) -> std::string
            {
                auto v = g(t);
                if (v)
                    return to_string_generic(*v);
                return std::string{};
            };
            return add(std::move(name), std::move(wrap));
        }

        void writeHeader()
        {
            if (header_written_)
                return;
            write_bom_if_needed(os_, opt_);
            names_.clear();
            names_.reserve(columns_.size());
            for (auto &c : columns_)
                names_.push_back(c.first);
            write_row_cells(names_);
            header_written_ = true;
        }

        void writeRow(const T &row)
        {
            ensure_header();
            buffer_.clear();
            buffer_.reserve(columns_.size());
            for (auto &[_, get] : columns_)
                buffer_.push_back(get(row));
            write_row_cells(buffer_);
        }

        template <class It>
        void writeRows(It first, It last)
        {
            ensure_header();
            for (; first != last; ++first)
                writeRow(*first);
        }

    private:
        void ensure_header()
        {
            if (!header_written_)
                writeHeader();
        }
        void write_row_cells(const std::vector<std::string> &cells)
        {
            for (size_t i = 0; i < cells.size(); ++i)
            {
                if (i)
                    os_.put(opt_.sep);
                write_cell(os_, cells[i], opt_);
            }
            os_ << opt_.newline;
        }

        std::ostream &os_;
        Options opt_;
        bool header_written_ = false;

        std::vector<std::pair<std::string, Getter>> columns_;
        std::vector<std::string> names_;
        std::vector<std::string> buffer_;
    };

} // namespace csv
