#include "valset.h"

///////////////////////////////////////////////////////////////////////////////
// ValueSet construction/destruction

ValueSet::ValueSet() { clear(); }

ValueSet::ValueSet(const ValueSet& set) {
    for (size_t i = 0; i < kWordCount; i++) set_[i] = set.set_[i];
}

ValueSet::~ValueSet() {}

///////////////////////////////////////////////////////////////////////////////
// ValueSet public methods

bool ValueSet::contains(int val) const {
    size_t word_no;
    int bit_no;
    fromValueToBitNo(val, word_no, bit_no);
    return set_[word_no] & (1ul << bit_no);
}

bool ValueSet::empty() const {
    for (size_t i = 0; i < kWordCount; i++)
        if (set_[i]) return false;
    return true;
}

bool ValueSet::isEqual(const ValueSet& set) const {
    for (size_t i = 0; i < kWordCount; i++)
        if (set_[i] != set.set_[i]) return false;
    return true;
}

int ValueSet::getFirstValue() const {
    int val = 0;
    for (size_t i = 0; i < kWordCount; i++) {
        unsigned long word = set_[i];
        for (size_t j = 0; j < kBitsPerWord; j++) {
            if (word & 1) return val;
            word >>= 1;
            val++;
        }
    }
    return -1;
}

int ValueSet::getNextValue(int val) const {
    size_t word_no;
    int bit_no;
    fromValueToBitNo(++val, word_no, bit_no);
    unsigned long word = set_[word_no] >> bit_no;
    // Current word
    for (size_t j = bit_no; j < kBitsPerWord; j++) {
        if (word & 1) return val;
        word >>= 1;
        val++;
    }
    // Other words
    for (size_t i = word_no + 1; i < kWordCount; i++) {
        word = set_[i];
        for (size_t j = 0; j < kBitsPerWord; j++) {
            if (word & 1) return val;
            word >>= 1;
            val++;
        }
    }
    return -1;
}

void ValueSet::addValue(int val) {
    size_t word_no;
    int bit_no;
    fromValueToBitNo(val, word_no, bit_no);
    set_[word_no] |= 1ul << bit_no;
}

void ValueSet::addValues(int from, int to) {
    size_t word1_no, word2_no;
    int bit1_no, bit2_no;
    assert(from <= to);
    fromValueToBitNo(from, word1_no, bit1_no);
    fromValueToBitNo(to, word2_no, bit2_no);
    if (word1_no == word2_no) {
        // All bits are inside the same word
        assert(bit1_no <= bit2_no);
        int shift = bit2_no - bit1_no + 1;
        if (shift < kBitsPerWord)
            set_[word1_no] |= ((1ul << shift) - 1) << bit1_no;
        else
            set_[word1_no] = ~0ul;
    } else {
        assert(word1_no <= word2_no);
        set_[word1_no] |= ~((1ul << bit1_no) - 1);
        int shift = bit2_no + 1;
        if (shift < kBitsPerWord)
            set_[word2_no] |= (1ul << shift) - 1;
        else
            set_[word2_no] = ~0ul;
        for (size_t i = (word1_no + 1); i < word2_no; i++) set_[i] = ~0ul;
    }
}

void ValueSet::removeValue(int val) {
    size_t word_no;
    int bit_no;
    fromValueToBitNo(val, word_no, bit_no);
    set_[word_no] &= ~(1ul << bit_no);
}

void ValueSet::removeValues(int from, int to) {
    size_t word1_no, word2_no;
    int bit1_no, bit2_no;
    assert(from <= to);
    fromValueToBitNo(from, word1_no, bit1_no);
    fromValueToBitNo(to, word2_no, bit2_no);
    if (word1_no == word2_no) {
        // All bits are inside the same word
        assert(bit1_no <= bit2_no);
        int shift = bit2_no - bit1_no + 1;
        if (shift < kBitsPerWord)
            set_[word1_no] &= ~(((1ul << shift) - 1) << bit1_no);
        else
            set_[word1_no] = 0ul;
    } else {
        assert(word1_no <= word2_no);
        set_[word1_no] &= (1ul << bit1_no) - 1;
        int shift = bit2_no + 1;
        if (shift < kBitsPerWord)
            set_[word2_no] &= ~((1ul << shift) - 1);
        else
            set_[word2_no] = 0ul;
        for (size_t i = (word1_no + 1); i < word2_no; i++) set_[i] = 0ul;
    }
}

void ValueSet::clear() {
    for (size_t i = 0; i < kWordCount; i++) set_[i] = 0ul;
}

///////////////////////////////////////////////////////////////////////////////
// ValueSet operators

ValueSet& ValueSet::operator=(const ValueSet& set) {
    if (&set != this) {
        for (size_t i = 0; i < kWordCount; i++) set_[i] = set.set_[i];
    }
    return *this;
}

ValueSet& ValueSet::operator-=(const ValueSet& set) {
    for (size_t i = 0; i < kWordCount; i++) set_[i] &= ~set.set_[i];
    return *this;
}

ValueSet& ValueSet::operator|=(const ValueSet& set) {
    for (size_t i = 0; i < kWordCount; i++) set_[i] |= set.set_[i];
    return *this;
}

ValueSet& ValueSet::operator&=(const ValueSet& set) {
    for (size_t i = 0; i < kWordCount; i++) set_[i] &= set.set_[i];
    return *this;
}

///////////////////////////////////////////////////////////////////////////////
// ValueSet private/protected methods

void ValueSet::fromValueToBitNo(int val, size_t& word_no, int& bit_no) const {
    assert((val >= 0) && (val <= kMaxValue));
    word_no = (unsigned)val / kBitsPerWord;
    assert(word_no < kWordCount);
    bit_no = val % kBitsPerWord;
}
