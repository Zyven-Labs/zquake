#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace zq {

// Dynamic array with growth factor
template<typename T, typename Allocator = void*>
class Array {
public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;

    Array() : data_(nullptr), size_(0), capacity_(0) {}
    
    explicit Array(size_type initial_capacity) 
        : data_(nullptr), size_(0), capacity_(0) {
        if (initial_capacity > 0) {
            Allocate(initial_capacity);
        }
    }
    
    Array(std::initializer_list<T> init)
        : data_(nullptr), size_(0), capacity_(0) {
        Reserve(init.size());
        for (const auto& item : init) {
            EmplaceBack(std::move(const_cast<T&>(item)));
        }
    }
    
    ~Array() {
        ClearAndDeallocate();
    }
    
    // Non-copyable (move-only)
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    
    // Movable
    Array(Array&& other) noexcept 
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            ClearAndDeallocate();
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
    
    // Element access
    reference operator[](size_type index) {
        return data_[index];
    }
    
    const_reference operator[](size_type index) const {
        return data_[index];
    }
    
    reference at(size_type index) {
        if (index >= size_) {
            throw std::out_of_range("Array::at - index out of bounds");
        }
        return data_[index];
    }
    
    const_reference at(size_type index) const {
        if (index >= size_) {
            throw std::out_of_range("Array::at - index out of bounds");
        }
        return data_[index];
    }
    
    reference front() { return data_[0]; }
    reference back() { return data_[size_ - 1]; }
    const_reference front() const { return data_[0]; }
    const_reference back() const { return data_[size_ - 1]; }
    
    T* data() { return data_; }
    const T* data() const { return data_; }
    
    // Capacity
    size_type size() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_type capacity() const { return capacity_; }
    
    void Reserve(size_type new_capacity) {
        if (new_capacity <= capacity_) return;
        Allocate(new_capacity);
    }
    
    void Resize(size_type new_size) {
        if (new_size == size_) return;
        if (new_size > size_) {
            Reserve(new_size);
            for (size_type i = size_; i < new_size; i++) {
                new (&data_[i]) T();
            }
        }
        // Truncate: destruct elements
        for (size_type i = new_size; i < size_; i++) {
            data_[i].~T();
        }
        size_ = new_size;
    }
    
    void ShrinkToFit() {
        if (size_ < capacity_ / 2 && capacity_ > 16) {
            T* new_data = static_cast<T*>(::operator new(size_ * sizeof(T)));
            for (size_type i = 0; i < size_; i++) {
                new (&new_data[i]) T(std::move(data_[i]));
                data_[i].~T();
            }
            ::operator delete(data_);
            data_ = new_data;
            capacity_ = size_;
        }
    }
    
    // Modifiers
    void Clear() {
        for (size_type i = 0; i < size_; i++) {
            data_[i].~T();
        }
        size_ = 0;
    }
    
    void ClearAndDeallocate() {
        Clear();
        ::operator delete(data_);
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }
    
    void PushBack(const T& value) {
        if (size_ == capacity_) {
            Grow(capacity_ == 0 ? 8 : capacity_ * 2);
        }
        new (&data_[size_]) T(value);
        size_++;
    }
    
    void PushBack(T&& value) {
        if (size_ == capacity_) {
            Grow(capacity_ == 0 ? 8 : capacity_ * 2);
        }
        new (&data_[size_]) T(std::move(value));
        size_++;
    }
    
    T PopBack() {
        if (size_ == 0) {
            throw std::out_of_range("Array::PopBack - array is empty");
        }
        T value = std::move(data_[size_ - 1]);
        data_[size_ - 1].~T();
        size_--;
        return value;
    }
    
    template<typename... Args>
    void EmplaceBack(Args&&... args) {
        if (size_ == capacity_) {
            Grow(capacity_ == 0 ? 8 : capacity_ * 2);
        }
        new (&data_[size_]) T(std::forward<Args>(args)...);
        size_++;
    }
    
    // Searching
    size_type Find(const T& value) const {
        for (size_type i = 0; i < size_; i++) {
            if (data_[i] == value) {
                return i;
            }
        }
        return npos;
    }
    
    bool Contains(const T& value) const {
        return Find(value) != npos;
    }
    
    // Removal
    void RemoveAt(size_type index) {
        if (index >= size_) {
            throw std::out_of_range("Array::RemoveAt - index out of bounds");
        }
        data_[index].~T();
        for (size_type i = index; i < size_ - 1; i++) {
            data_[i] = std::move(data_[i + 1]);
        }
        size_--;
    }
    
    void RemoveSwapBack(size_type index) {
        if (index >= size_) {
            throw std::out_of_range("Array::RemoveSwapBack - index out of bounds");
        }
        data_[index].~T();
        if (index < size_ - 1) {
            data_[index] = std::move(data_[size_ - 1]);
        }
        size_--;
    }
    
    void Swap(size_type a, size_type b) {
        if (a >= size_ || b >= size_) {
            throw std::out_of_range("Array::Swap - index out of bounds");
        }
        std::swap(data_[a], data_[b]);
    }
    
    static constexpr size_type npos = size_type(-1);
    
private:
    void Allocate(size_type new_capacity) {
        capacity_ = new_capacity;
        data_ = static_cast<T*>(::operator new(capacity_ * sizeof(T)));
    }
    
    void Grow(size_type new_capacity) {
        Reserve(new_capacity);
    }
    
    T* data_;
    size_type size_;
    size_type capacity_;
};

// Alias for common use cases
using IntArray = Array<int>;
using FloatArray = Array<float>;
using StringArray = Array<char>;

} // namespace zq
