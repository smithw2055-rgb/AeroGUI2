#pragma once

#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace aero {

template <typename T, std::size_t ChunkSize = 256u>
class ChunkedArena final {
    static_assert(ChunkSize > 0u, "ChunkedArena ChunkSize must be positive.");

    struct Chunk {
        alignas(T) std::byte data[sizeof(T) * ChunkSize];
    };

public:
    template <bool IsConst>
    class IteratorImpl {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = std::conditional_t<IsConst, const T*, T*>;
        using reference = std::conditional_t<IsConst, const T&, T&>;

        IteratorImpl() noexcept = default;
        IteratorImpl(std::conditional_t<IsConst, const ChunkedArena*, ChunkedArena*> arena, std::size_t index) noexcept
            : arena_(arena), index_(index) {}

        [[nodiscard]] reference operator*() const noexcept { return (*arena_)[index_]; }
        [[nodiscard]] pointer operator->() const noexcept { return &(*arena_)[index_]; }
        IteratorImpl& operator++() noexcept {
            ++index_;
            return *this;
        }
        IteratorImpl operator++(int) noexcept {
            IteratorImpl tmp = *this;
            ++index_;
            return tmp;
        }
        [[nodiscard]] bool operator==(const IteratorImpl& rhs) const noexcept {
            return arena_ == rhs.arena_ && index_ == rhs.index_;
        }
        [[nodiscard]] bool operator!=(const IteratorImpl& rhs) const noexcept {
            return !(*this == rhs);
        }

    private:
        std::conditional_t<IsConst, const ChunkedArena*, ChunkedArena*> arena_ = nullptr;
        std::size_t index_ = 0u;
    };

    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

    ChunkedArena() noexcept = default;
    ~ChunkedArena() noexcept { clear(); }

    ChunkedArena(const ChunkedArena&) = delete;
    ChunkedArena& operator=(const ChunkedArena&) = delete;
    ChunkedArena(ChunkedArena&&) noexcept = default;
    ChunkedArena& operator=(ChunkedArena&&) noexcept = default;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunks_.size(); }
    [[nodiscard]] std::size_t capacity_bytes() const noexcept { return chunks_.size() * sizeof(Chunk); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0u; }

    [[nodiscard]] T& operator[](std::size_t idx) noexcept {
        return *std::launder(reinterpret_cast<T*>(
            chunks_[idx / ChunkSize]->data + (idx % ChunkSize) * sizeof(T)));
    }

    [[nodiscard]] const T& operator[](std::size_t idx) const noexcept {
        return *std::launder(reinterpret_cast<const T*>(
            chunks_[idx / ChunkSize]->data + (idx % ChunkSize) * sizeof(T)));
    }

    [[nodiscard]] T& front() noexcept { return (*this)[0u]; }
    [[nodiscard]] const T& front() const noexcept { return (*this)[0u]; }
    [[nodiscard]] T& back() noexcept { return (*this)[size_ - 1u]; }
    [[nodiscard]] const T& back() const noexcept { return (*this)[size_ - 1u]; }

    template <typename... Args>
    T& emplace_back(Args&&... args) {
        const std::size_t chunkIdx = size_ / ChunkSize;
        if (chunkIdx >= chunks_.size()) {
            chunks_.push_back(std::make_unique<Chunk>());
        }
        const std::size_t slotIdx = size_ % ChunkSize;
        T* ptr = ::new (static_cast<void*>(chunks_[chunkIdx]->data + slotIdx * sizeof(T)))
            T(std::forward<Args>(args)...);
        ++size_;
        return *ptr;
    }

    void push_back(const T& val) { (void)emplace_back(val); }
    void push_back(T&& val) { (void)emplace_back(std::move(val)); }

    void clear() noexcept {
        for (std::size_t i = size_; i > 0u; --i) {
            (*this)[i - 1u].~T();
        }
        size_ = 0u;
        chunks_.clear();
    }

    [[nodiscard]] iterator begin() noexcept { return iterator(this, 0u); }
    [[nodiscard]] iterator end() noexcept { return iterator(this, size_); }
    [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0u); }
    [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, size_); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return const_iterator(this, 0u); }
    [[nodiscard]] const_iterator cend() const noexcept { return const_iterator(this, size_); }

private:
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::size_t size_ = 0u;
};

} // namespace aero
