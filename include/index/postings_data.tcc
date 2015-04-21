/**
 * @file postings_data.tcc
 * @author Sean Massung
 */

#include <algorithm>
#include <cstring>
#include <numeric>
#include "index/postings_data.h"

namespace meta
{
namespace index
{

template <class PrimaryKey, class SecondaryKey>
postings_data<PrimaryKey, SecondaryKey>::postings_data(PrimaryKey p_id)
    : p_id_{p_id}
{ /* nothing */
}

template <class PrimaryKey, class SecondaryKey>
void postings_data<PrimaryKey, SecondaryKey>::merge_with(postings_data& other)
{
    auto searcher = [](const pair_t& p, const SecondaryKey& s)
    {
        return p.first < s;
    };

    // O(n log n) now, could be O(n)

    // if the primary_key doesn't exist, add onto back
    uint64_t orig_length = counts_.size();
    for (auto& p : other.counts_)
    {
        auto it = std::lower_bound(
            counts_.begin(), counts_.begin() + orig_length, p.first, searcher);
        if (it == counts_.end() || it->first != p.first)
            counts_.emplace_back(std::move(p));
        else
            it->second += p.second;
    }

    // sort counts_ again to fix new elements added onto back
    if (counts_.size() > orig_length)
    {
        std::sort(counts_.begin(), counts_.end(),
                  [](const pair_t& a, const pair_t& b)
                  {
                      return a.first < b.first;
                  });
    }
}

template <class PrimaryKey, class SecondaryKey>
void postings_data<PrimaryKey, SecondaryKey>::increase_count(SecondaryKey s_id,
                                                             double amount)
{
    counts_[s_id] += amount;
}

template <class PrimaryKey, class SecondaryKey>
double postings_data<PrimaryKey, SecondaryKey>::count(SecondaryKey s_id) const
{
    return counts_.at(s_id);
}

template <class PrimaryKey, class SecondaryKey>
const std::vector<std::pair<SecondaryKey, double>>&
    postings_data<PrimaryKey, SecondaryKey>::counts() const
{
    return counts_.contents();
}

template <class PrimaryKey, class SecondaryKey>
void postings_data<PrimaryKey, SecondaryKey>::set_counts(const count_t& counts)
{
    // no sort needed: sparse_vector::contents() sorts the parameter
    counts_.contents(counts);
}

template <class PrimaryKey, class SecondaryKey>
template <class InputIterator>
void postings_data<PrimaryKey, SecondaryKey>::set_counts(InputIterator begin,
                                                         InputIterator end)
{
    for (; begin != end; ++begin)
        counts_.emplace_back(*begin);
    counts_.shrink_to_fit();
}

template <class PrimaryKey, class SecondaryKey>
void postings_data<PrimaryKey, SecondaryKey>::set_primary_key(
    PrimaryKey new_key)
{
    p_id_ = new_key;
}

template <class PrimaryKey, class SecondaryKey>
bool postings_data<PrimaryKey, SecondaryKey>::
    operator<(const postings_data& other) const
{
    return primary_key() < other.primary_key();
}

template <class PrimaryKey, class SecondaryKey>
bool operator==(const postings_data<PrimaryKey, SecondaryKey>& lhs,
                const postings_data<PrimaryKey, SecondaryKey>& rhs)
{
    return lhs.primary_key() == rhs.primary_key();
}

template <class PrimaryKey, class SecondaryKey>
PrimaryKey postings_data<PrimaryKey, SecondaryKey>::primary_key() const
{
    return p_id_;
}

template <class PrimaryKey, class SecondaryKey>
template <class FeatureValue>
void postings_data<PrimaryKey, SecondaryKey>::write_compressed(
    io::compressed_file_writer& writer) const
{
    writer.write(counts_.size());
    writer.write(std::accumulate(counts_.begin(), counts_.end(), uint64_t{0},
                                 [](uint64_t cur, const pair_t& pr)
                                 {
                                     return cur
                                            + static_cast<uint64_t>(pr.second);
                                 }));
    count_t mutable_counts{counts_.contents()};
    writer.write(mutable_counts[0].first);
    if (std::is_same<FeatureValue, uint64_t>::value)
    {
        writer.write(static_cast<uint64_t>(mutable_counts[0].second));
    }
    else
    {
        writer.write(mutable_counts[0].second);
    }

    // use gap encoding on the SecondaryKeys (we know they are integral types)
    uint64_t cur_id = mutable_counts[0].first;
    for (size_t i = 1; i < mutable_counts.size(); ++i)
    {
        uint64_t temp_id = mutable_counts[i].first;
        mutable_counts[i].first = mutable_counts[i].first - cur_id;
        cur_id = temp_id;

        writer.write(mutable_counts[i].first);
        if (std::is_same<FeatureValue, uint64_t>::value)
        {
            writer.write(static_cast<uint64_t>(mutable_counts[i].second));
        }
        else
        {
            writer.write(mutable_counts[i].second);
        }
    }
}

template <class PrimaryKey, class SecondaryKey>
template <class FeatureValue>
void postings_data<PrimaryKey, SecondaryKey>::read_compressed(
    io::compressed_file_reader& reader)
{
    uint64_t size = reader.next();

    // ignore total counts sum
    reader.next();

    counts_.clear();
    counts_.reserve(size);

    uint64_t last_id = 0;

    for (uint64_t i = 0; i < size; ++i)
    {
        uint64_t this_id = reader.next();
        // we're using gap encoding
        last_id += this_id;
        SecondaryKey key{last_id};

        double count;
        if (std::is_same<FeatureValue, uint64_t>::value)
        {
            uint64_t next = reader.next();
            count = static_cast<double>(next);
        }
        else
        {
            count = reader.next_double();
        }

        counts_.emplace_back(key, count);
    }

    // compress vector to conserve memory (it shouldn't be modified again after
    // this)
    counts_.shrink_to_fit();
}

namespace
{
template <class T>
uint64_t length(const T& elem,
                typename std::enable_if<std::is_same<T, std::string>::value>::
                    type* = nullptr)
{
    return elem.size();
}

template <class T>
uint64_t length(const T& elem,
                typename std::enable_if<!std::is_same<T, std::string>::value>::
                    type* = nullptr)
{
    return sizeof(elem);
}
}

template <class PrimaryKey, class SecondaryKey>
uint64_t postings_data<PrimaryKey, SecondaryKey>::bytes_used() const
{
    return sizeof(pair_t) * counts_.size() + length(p_id_);
}
}
}
