#include "valset.h"

#include <algorithm>

bool ValueSet::empty() const {
    return std::all_of(set_.begin(), set_.end(), [](const auto& w) { return w == 0; });
}

unsigned ValueSet::getFirstValue() const {
    unsigned v = 0;
    for (const auto& w : set_) {
        if (w) { return v + detail::lsb(w); }
        v += kBitsPerWord;
    }
    return v;
}

unsigned ValueSet::getNextValue(unsigned v) const {
    assert(v <= kMaxValue);
    auto it = set_.begin() + nword(v++);
    if (unsigned n = nbit(v); n && (*it >> n)) {
        v += detail::lsb(*it >> n);
    } else {
        v = detail::alignUp(v, kBitsPerWord);
        for (++it; it != set_.end(); ++it, v += kBitsPerWord) {
            if (*it) { return v + detail::lsb(*it); }
        }
    }
    return v;
}

ValueSet& ValueSet::addValues(unsigned from, unsigned to) {
    assert(from <= to && to <= kMaxValue);
    auto it = set_.begin() + nword(from), it_last = set_.begin() + nword(++to);
    if (it == it_last) {
        *it |= ~(bitmask(from) - 1) & (bitmask(to) - 1);
    } else {
        *it++ |= ~(bitmask(from) - 1);
        while (it != it_last) { *it++ = ~0ul; }
        if (it_last != set_.end()) { *it_last |= bitmask(to) - 1; }
    }
    return *this;
}

ValueSet& ValueSet::removeValues(unsigned from, unsigned to) {
    assert(from <= to && to <= kMaxValue);
    auto it = set_.begin() + nword(from), it_last = set_.begin() + nword(++to);
    if (it == it_last) {
        *it &= (bitmask(from) - 1) | ~(bitmask(to) - 1);
    } else {
        *it++ &= bitmask(from) - 1;
        while (it != it_last) { *it++ = 0ul; }
        if (it_last != set_.end()) { *it_last &= ~(bitmask(to) - 1); }
    }
    return *this;
}

ValueSet& ValueSet::operator|=(const ValueSet& rhs) {
    std::transform(set_.begin(), set_.end(), rhs.set_.begin(), set_.begin(),
                   [](const auto& w1, const auto& w2) { return w1 | w2; });
    return *this;
}

ValueSet& ValueSet::operator&=(const ValueSet& rhs) {
    std::transform(set_.begin(), set_.end(), rhs.set_.begin(), set_.begin(),
                   [](const auto& w1, const auto& w2) { return w1 & w2; });
    return *this;
}

ValueSet& ValueSet::operator^=(const ValueSet& rhs) {
    std::transform(set_.begin(), set_.end(), rhs.set_.begin(), set_.begin(),
                   [](const auto& w1, const auto& w2) { return w1 ^ w2; });
    return *this;
}

ValueSet& ValueSet::operator-=(const ValueSet& rhs) {
    std::transform(set_.begin(), set_.end(), rhs.set_.begin(), set_.begin(),
                   [](const auto& w1, const auto& w2) { return w1 & ~w2; });
    return *this;
}
