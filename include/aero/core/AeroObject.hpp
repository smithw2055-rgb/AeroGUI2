#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "aero/core/TypeClass.hpp"

namespace aero {

template <typename T>
class IntrusivePtr {
public:
    constexpr IntrusivePtr() noexcept = default;
    constexpr IntrusivePtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    explicit IntrusivePtr(T* p, bool addRef = true) noexcept : ptr_(p) {
        if (ptr_ && addRef) {
            ptr_->AddRef();
        }
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    IntrusivePtr(const IntrusivePtr<U>& other) noexcept : ptr_(other.Get()) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    IntrusivePtr(IntrusivePtr<U>&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~IntrusivePtr() {
        Reset();
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            Reset(other.ptr_);
        }
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->Release();
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    IntrusivePtr& operator=(IntrusivePtr<U>&& other) noexcept {
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        return *this;
    }

    IntrusivePtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    void Reset(T* p = nullptr, bool addRef = true) noexcept {
        if (p && addRef) {
            p->AddRef();
        }
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = p;
    }

    [[nodiscard]] T* Detach() noexcept {
        T* p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    [[nodiscard]] T* Get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    template <typename U>
    friend class IntrusivePtr;

    T* ptr_ = nullptr;
};

template <typename T, typename U>
[[nodiscard]] inline bool operator==(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept {
    return lhs.Get() == rhs.Get();
}

template <typename T, typename U>
[[nodiscard]] inline bool operator!=(const IntrusivePtr<T>& lhs, const IntrusivePtr<U>& rhs) noexcept {
    return lhs.Get() != rhs.Get();
}

template <typename T>
[[nodiscard]] inline bool operator==(const IntrusivePtr<T>& lhs, std::nullptr_t) noexcept {
    return lhs.Get() == nullptr;
}

template <typename T>
[[nodiscard]] inline bool operator==(std::nullptr_t, const IntrusivePtr<T>& rhs) noexcept {
    return rhs.Get() == nullptr;
}

template <typename T>
[[nodiscard]] inline bool operator!=(const IntrusivePtr<T>& lhs, std::nullptr_t) noexcept {
    return lhs.Get() != nullptr;
}

template <typename T>
[[nodiscard]] inline bool operator!=(std::nullptr_t, const IntrusivePtr<T>& rhs) noexcept {
    return rhs.Get() != nullptr;
}

template <typename T, typename... Args>
[[nodiscard]] IntrusivePtr<T> MakeIntrusive(Args&&... args) {
    return IntrusivePtr<T>(new T(std::forward<Args>(args)...), true);
}

class AeroObject {
public:
    using SelfClass = AeroObject;
    using ParentClass = void;

    AeroObject() noexcept = default;
    virtual ~AeroObject() = default;

    AeroObject(const AeroObject&) = delete;
    AeroObject& operator=(const AeroObject&) = delete;

    void AddRef() const noexcept {
        refCount_.fetch_add(1u, std::memory_order_relaxed);
    }

    void Release() const noexcept {
        if (refCount_.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            OnDestroy();
        }
    }

    [[nodiscard]] std::uint32_t RefCount() const noexcept {
        return refCount_.load(std::memory_order_relaxed);
    }

    static const TypeClass* StaticTypeClass() noexcept;

    [[nodiscard]] virtual const TypeClass* GetTypeClass() const noexcept {
        return StaticTypeClass();
    }

    [[nodiscard]] bool IsA(const TypeClass* target) const noexcept {
        if (target == nullptr) {
            return false;
        }
        const TypeClass* selfType = GetTypeClass();
        if (selfType == target) {
            return true;
        }
        TypeRegistry::Instance().EnsureIntervalsUpToDate();
        return selfType != nullptr && selfType->IsDerivedFrom(target);
    }

    template <typename T>
    [[nodiscard]] bool IsA() const noexcept {
        return IsA(T::StaticTypeClass());
    }

    template <typename T>
    [[nodiscard]] T* DynamicCast() noexcept {
        return IsA<T>() ? static_cast<T*>(this) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* DynamicCast() const noexcept {
        return IsA<T>() ? static_cast<const T*>(this) : nullptr;
    }

protected:
    virtual void OnDestroy() const noexcept {
        delete this;
    }

private:
    mutable std::atomic<std::uint32_t> refCount_{0u};
};

} // namespace aero
