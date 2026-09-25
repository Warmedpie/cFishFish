// TT.h : Transposition table for cFishFish.
//
// Caches search results by Zobrist key so positions reached by different
// move orders (transpositions) and positions revisited by iterative
// deepening don't have to be searched again.
//
// One entry per slot, table size is a power of two so indexing is a mask.
// Replacement: a slot holding an older search's entry or a shallower result
// is overwritten; a deeper entry from the current search is kept.

#pragma once

#include <algorithm>
#include <cstdint>
#include <new>
#include <vector>

#include "Chess.h"
#include "Eval.h"

namespace search {

enum class Bound : std::uint8_t {
    None,
    Exact,  // score is exact (alpha < score < beta)
    Lower,  // fail high: real score >= stored score
    Upper,  // fail low:  real score <= stored score
};

struct TTEntry {
    std::uint64_t key = 0;      // full Zobrist key, to reject index collisions
    std::uint16_t move = 0;     // best move (chess::Move raw value), 0 = none
    std::int16_t score = 0;     // mate scores stored relative to this node
    std::int8_t depth = 0;      // remaining depth searched (0 = qsearch)
    Bound bound = Bound::None;
    std::uint8_t generation = 0;  // which "go" wrote this entry
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

    // Resize to the largest power-of-two entry count that fits in `mb`.
    // Contents are cleared.
    void resize(std::size_t mb) {
        const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024 * 1024;
        std::size_t count = 1;
        while (count * 2 * sizeof(TTEntry) <= bytes) count *= 2;

        for (;;) {
            try {
                table_.assign(count, TTEntry{});
                table_.shrink_to_fit();
                break;
            } catch (const std::bad_alloc&) {
                if (count <= 1024) throw;
                count /= 2;  // not enough memory: try half the size
            }
        }
        mask_ = count - 1;
        generation_ = 1;
    }

    void clear() {
        std::fill(table_.begin(), table_.end(), TTEntry{});
        generation_ = 1;
    }

    // Call once at the start of every "go" so old entries can be recognized
    // and replaced first.
    void new_search() {
        if (++generation_ == 0) generation_ = 1;  // 0 marks never-written slots
    }

    // Returns true and fills `out` if this key is in the table.
    bool probe(std::uint64_t key, TTEntry& out) const {
        const TTEntry& e = table_[key & mask_];
        if (e.key != key || e.bound == Bound::None) return false;
        out = e;
        return true;
    }

    void store(std::uint64_t key, chess::Move move, eval::Score score, int depth, Bound bound) {
        TTEntry& e = table_[key & mask_];
        const bool same = (e.key == key);

        // Keep the old best move if this search didn't find one (fail low).
        std::uint16_t m = move.move();
        if (m == 0 && same) m = e.move;

        const bool replace = same ? (bound == Bound::Exact || depth + 2 >= e.depth)
                                  : (e.generation != generation_ || depth >= e.depth);
        if (!replace) return;

        e.key = key;
        e.move = m;
        e.score = static_cast<std::int16_t>(score);
        e.depth = static_cast<std::int8_t>(std::clamp(depth, 0, 127));
        e.bound = bound;
        e.generation = generation_;
    }

    // UCI "hashfull": permille of the first 1000 slots used by this search.
    int hashfull() const {
        const std::size_t n = std::min<std::size_t>(1000, table_.size());
        int used = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (table_[i].bound != Bound::None && table_[i].generation == generation_) ++used;
        return static_cast<int>(used * 1000 / n);
    }

    std::size_t size() const { return table_.size(); }

private:
    std::vector<TTEntry> table_;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 1;
};

}  // namespace search
