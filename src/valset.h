#pragma once

#include <cassert>
#include <cstddef>

// ValueSet class
class ValueSet {
    friend ValueSet operator-(const ValueSet& set1, const ValueSet& set2) {
        ValueSet ret = set1;
        return (ret -= set2);
    };
    friend ValueSet operator|(const ValueSet& set1, const ValueSet& set2) {
        ValueSet ret = set1;
        return (ret |= set2);
    };
    friend ValueSet operator&(const ValueSet& set1, const ValueSet& set2) {
        ValueSet ret = set1;
        return (ret &= set2);
    };
    friend bool operator==(const ValueSet& set1, const ValueSet& set2) { return set1.isEqual(set2); };
    friend bool operator!=(const ValueSet& set1, const ValueSet& set2) { return !set1.isEqual(set2); };
    // Interface
 public:
    ValueSet();
    ValueSet(const ValueSet&);
    ~ValueSet();

    static const size_t kMaxValue = 1023;

    bool contains(int val) const;
    bool empty() const;
    bool isEqual(const ValueSet& set) const;
    int getFirstValue() const;
    int getNextValue(int val) const;
    void addValue(int val);
    void addValues(int from, int to);
    void removeValue(int val);
    void removeValues(int from, int to);
    void clear();

    ValueSet& operator=(const ValueSet& set);
    ValueSet& operator-=(const ValueSet& set);
    ValueSet& operator|=(const ValueSet& set);
    ValueSet& operator&=(const ValueSet& set);

    // Implementation
 protected:
    static const size_t kBitsPerWord = 8 * sizeof(unsigned long);
    static const size_t kWordCount = (kMaxValue + 1) / kBitsPerWord;
    unsigned long set_[kWordCount];  // Bit array for presence indication

    void fromValueToBitNo(int val, size_t& word_no, int& bit_no) const;
};
