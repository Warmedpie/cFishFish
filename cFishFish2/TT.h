// TT.h : Transposition table for cFishFish.
//
// Caches search results by Zobrist key so positions reached by different
// move orders (transpositions) and positions revisited by iterative
// deepening don't have to be searched again.
//
// One entry per slot, table size is a power of two so indexing is a mask.
// Replacement: a slot holding an older search's entry or a shallower result
// is overwritten; a deeper entry from the current search is kept.
//
// Each entry also caches the position's raw static eval (SP_TT_EVAL), so a
// revisited node that doesn't cut off skips the evaluation.
//
// Shared by all search threads without locks. Each slot is two 64-bit words,
// the data and (key XOR data), written and read with relaxed atomics. If two
// threads write a slot at the same time, or a read sees half of a write, the
// key check fails and the entry is simply treated as missing.

#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

#include "Chess.h"
#include "Eval.h"

namespace search {

inline constexpr std::int16_t TTNoEval = INT16_MIN;  // no eval stored (e.g. in check)

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
    std::uint8_t generation = 0;  // which "go" wrote this entry (6 bits)
    std::int16_t eval = TTNoEval;  // raw static eval, side to move's view
};

class TranspositionTable {
public:
    explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

    // Resize to the largest power-of-two entry count that fits in `mb`.
    // Contents are cleared.
    void resize(std::size_t mb) {
        const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024 * 1024;
        std::size_t count = 1;
        while (count * 2 * sizeof(Slot) <= bytes) count *= 2;

        for (;;) {
            try {
                table_.reset();
                table_ = std::make_unique<Slot[]>(count);
                break;
            } catch (const std::bad_alloc&) {
                if (count <= 1024) throw;
                count /= 2;  // not enough memory: try half the size
            }
        }
        size_ = count;
        mask_ = count - 1;
        clear();
    }

    void clear() {
        for (std::size_t i = 0; i < size_; ++i) {
            table_[i].data.store(0, std::memory_order_relaxed);
            table_[i].check.store(0, std::memory_order_relaxed);
        }
        generation_ = 1;
    }

    // Call once at the start of every "go" so old entries can be recognized
    // and replaced first.
    void new_search() {
        generation_ = static_cast<std::uint8_t>((generation_ + 1) & 63);
        if (generation_ == 0) generation_ = 1;  // 0 marks never-written slots
    }

    // Returns true and fills `out` if this key is in the table.
    bool probe(std::uint64_t key, TTEntry& out) const {
        TTEntry e;
        if (!read(key & mask_, e) || e.key != key || e.bound == Bound::None) return false;
        out = e;
        return true;
    }

    // Like probe(), but also finds eval-only entries (Bound::None).
    bool probe_any(std::uint64_t key, TTEntry& out) const {
        TTEntry e;
        if (!read(key & mask_, e) || e.key != key) return false;
        out = e;
        return true;
    }

    void store(std::uint64_t key, chess::Move move, eval::Score score, int depth, Bound bound,
               int static_eval = TTNoEval) {
        const std::size_t i = key & mask_;
        TTEntry e;
        const bool valid = read(i, e);
        const bool same = valid && (e.key == key);
        if (!valid) e = TTEntry{};

        // Keep the old best move if this search didn't find one (fail low).
        std::uint16_t m = move.move();
        if (m == 0 && same) m = e.move;
        std::int16_t ev = static_cast<std::int16_t>(static_eval);
        if (ev == TTNoEval && same) ev = e.eval;  // keep the eval we already had

        const bool replace = same ? (bound == Bound::Exact || depth + 2 >= e.depth)
                                  : (e.generation != generation_ || depth >= e.depth);
        if (!replace) return;

        e.key = key;
        e.move = m;
        e.score = static_cast<std::int16_t>(score);
        e.depth = static_cast<std::int8_t>(std::clamp(depth, 0, 127));
        e.bound = bound;
        e.generation = generation_;
        e.eval = ev;
        write(i, e);
    }

    // UCI "hashfull": permille of the first 1000 slots used by this search.
    int hashfull() const {
        const std::size_t n = std::min<std::size_t>(1000, size_);
        int used = 0;
        for (std::size_t i = 0; i < n; ++i) {
            TTEntry e;
            if (read(i, e) && e.bound != Bound::None && e.generation == generation_) ++used;
        }
        return static_cast<int>(used * 1000 / n);
    }

    std::size_t size() const { return size_; }

private:
    struct Slot {
        std::atomic<std::uint64_t> data{0};   // move | score | depth | bound:2 generation:6 | eval
        std::atomic<std::uint64_t> check{0};  // key ^ data
    };

    static std::uint64_t pack(const TTEntry& e) {
        return static_cast<std::uint64_t>(e.move) |
               static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.score)) << 16 |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(e.depth)) << 32 |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(e.bound)) << 40 |
               static_cast<std::uint64_t>(e.generation & 63) << 42 |
               static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.eval)) << 48;
    }

    // Reads slot i; false if it is empty. e.key is recovered from the check
    // word, so a torn read yields a key that won't match the one probed.
    bool read(std::size_t i, TTEntry& e) const {
        const std::uint64_t d = table_[i].data.load(std::memory_order_relaxed);
        const std::uint64_t c = table_[i].check.load(std::memory_order_relaxed);
        if (d == 0 && c == 0) return false;
        e.key = c ^ d;
        e.move = static_cast<std::uint16_t>(d);
        e.score = static_cast<std::int16_t>(static_cast<std::uint16_t>(d >> 16));
        e.depth = static_cast<std::int8_t>(static_cast<std::uint8_t>(d >> 32));
        e.bound = static_cast<Bound>(static_cast<std::uint8_t>((d >> 40) & 3));
        e.generation = static_cast<std::uint8_t>((d >> 42) & 63);
        e.eval = static_cast<std::int16_t>(static_cast<std::uint16_t>(d >> 48));
        return true;
    }

    void write(std::size_t i, const TTEntry& e) {
        const std::uint64_t d = pack(e);
        table_[i].data.store(d, std::memory_order_relaxed);
        table_[i].check.store(e.key ^ d, std::memory_order_relaxed);
    }

    std::unique_ptr<Slot[]> table_;
    std::size_t size_ = 0;
    std::size_t mask_ = 0;
    std::uint8_t generation_ = 1;
};

}  // namespace search
