module;
#include <cstddef>
#include <new>
#include <vector>
module slotmap:storage.page_pool;

namespace inco {
    // thread local freelist
    // each instance of the slot map that has the same size and alignment
    // shares a freelist
    template <std::size_t Bytes, std::size_t Align>
    struct PagePool {
        // retain at most this many bytes of pages per thread
        static constexpr std::size_t budget_bytes = std::size_t{192} << 20;
        static constexpr std::size_t cap =
            (budget_bytes / Bytes) < 8 ? 8 : (budget_bytes / Bytes);

        // frees whatever is still pooled when the thread exits
        struct FreeList {
            std::vector<void*> v;

            ~FreeList() {
                for (void* p : v)
                    ::operator delete(p, std::align_val_t{Align});
            }
        };

        static std::vector<void*>& list() noexcept {
            static thread_local FreeList fl;
            return fl.v;
        }

        [[nodiscard]] static void* acquire() {
            if (auto& l = list(); !l.empty()) {
                void* p = l.back();
                l.pop_back();
                return p;
            }
            return ::operator new(Bytes, std::align_val_t{Align});
        }

        static void release(void* p) noexcept {
            if (!p) return;
            if (auto& l = list(); l.size() < cap) {
                // push_back can throw on OOM
                try {
                    l.push_back(p);
                    return;
                } catch (...) {
                }
            }
            ::operator delete(p, std::align_val_t{Align});
        }
    };
}