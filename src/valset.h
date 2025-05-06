#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>

namespace detail {
constexpr unsigned lsb(unsigned long v) {
    unsigned r = 0;
    while (!(v & 1)) { v >>= 1, ++r; }
    return r;
}
constexpr unsigned alignUp(unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); }
}  // namespace detail

class ValueSet {
 public:
    ValueSet() { set_.fill(0); }
    ValueSet(unsigned from, unsigned to) {
        set_.fill(0);
        addValues(from, to);
    }

    static const unsigned kMaxValue = 1023;

    class Iterator {
     public:
        using iterator_category = std::input_iterator_tag;
        using value_type = unsigned;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        Iterator() = default;

        Iterator& operator++() {
            v_ = vset_->getNextValue(v_);
            return *this;
        }
        reference operator*() const { return v_; }
        pointer operator->() const { return std::addressof(**this); }

        Iterator operator++(int) {
            auto it = *this;
            ++*this;
            return it;
        }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs) {
            assert(lhs.vset_ == rhs.vset_);
            return lhs.v_ == rhs.v_;
        };
        friend bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !(lhs == rhs); };

     private:
        const ValueSet* vset_ = nullptr;
        unsigned v_ = 0;
        friend class ValueSet;
        Iterator(const ValueSet* vset, unsigned v) : vset_(vset), v_(v) {}
    };

    bool empty() const;
    Iterator begin() const { return Iterator(this, getFirstValue()); }
    Iterator end() const { return Iterator(this, kMaxValue + 1); }
    unsigned getFirstValue() const;
    unsigned getNextValue(unsigned v) const;
    bool contains(unsigned v) const {
        assert(v <= kMaxValue);
        return set_[nword(v)] & bitmask(v);
    }

    ValueSet& clear() {
        set_.fill(0);
        return *this;
    }
    ValueSet& addValue(unsigned v) {
        assert(v <= kMaxValue);
        set_[nword(v)] |= bitmask(v);
        return *this;
    }
    ValueSet& addValues(unsigned from, unsigned to);
    ValueSet& removeValue(unsigned v) {
        assert(v <= kMaxValue);
        set_[nword(v)] &= ~bitmask(v);
        return *this;
    }
    ValueSet& removeValues(unsigned from, unsigned to);

    ValueSet& operator|=(const ValueSet& rhs);
    ValueSet& operator&=(const ValueSet& rhs);
    ValueSet& operator^=(const ValueSet& rhs);
    ValueSet& operator-=(const ValueSet& rhs);

    friend ValueSet operator|(const ValueSet& lhs, const ValueSet& rhs) {
        ValueSet ret = lhs;
        return ret |= rhs;
    };
    friend ValueSet operator&(const ValueSet& lhs, const ValueSet& rhs) {
        ValueSet ret = lhs;
        return ret &= rhs;
    };
    friend ValueSet operator^(const ValueSet& lhs, const ValueSet& rhs) {
        ValueSet ret = lhs;
        return ret ^= rhs;
    };
    friend ValueSet operator-(const ValueSet& lhs, const ValueSet& rhs) {
        ValueSet ret = lhs;
        return ret -= rhs;
    };
    friend bool operator==(const ValueSet& lhs, const ValueSet& rhs) { return lhs.set_ == rhs.set_; };
    friend bool operator!=(const ValueSet& lhs, const ValueSet& rhs) { return lhs.set_ != rhs.set_; };

 protected:
    static constexpr unsigned kBitsPerWord = 8 * sizeof(unsigned long);
    static constexpr unsigned kBit2WordShift = detail::lsb(kBitsPerWord);
    static unsigned nword(unsigned v) { return v >> kBit2WordShift; }
    static unsigned nbit(unsigned v) { return v & (kBitsPerWord - 1); }
    static unsigned long bitmask(unsigned v) { return 1ul << nbit(v); }

    // Bit array for presence indication
    std::array<unsigned long, (kMaxValue + 1) / kBitsPerWord> set_;
};
