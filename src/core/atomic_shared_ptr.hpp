#pragma once

#include <atomic>
#include <memory>
#include <utility>

namespace training {
// Some C++20 libraries implement atomic shared_ptr through the standard free
// functions but do not yet provide the C++20 atomic specialization.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
template<class T> using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;
#else
template<class T> class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> value):value_(std::move(value)) {}
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    std::shared_ptr<T> load(std::memory_order order=std::memory_order_seq_cst) const {
        return std::atomic_load_explicit(&value_,order);
    }
    void store(std::shared_ptr<T> value,std::memory_order order=std::memory_order_seq_cst) {
        std::atomic_store_explicit(&value_,std::move(value),order);
    }
private:
    std::shared_ptr<T> value_;
};
#endif
}
