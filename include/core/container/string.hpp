#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <stdexcept>
#include "core/container/array.hpp"

namespace zq {

class String {
public:
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using value_type = char;
    using reference = char&;
    using const_reference = const char&;
    using pointer = char*;
    using const_pointer = const char*;
    using iterator = char*;
    using const_iterator = const char*;
    
    String() : data_(nullptr), size_(0), capacity_(0) {}
    
    String(std::string_view sv) : data_(nullptr), size_(0), capacity_(0) {
        Assign(sv.data(), sv.size());
    }
    
    String(const char* str) : data_(nullptr), size_(0), capacity_(0) {
        if (str) Assign(str, strlen(str));
    }
    
    String(const char* str, size_type len) : data_(nullptr), size_(0), capacity_(0) {
        Assign(str, len);
    }
    
    String(const String& other) : data_(nullptr), size_(0), capacity_(0) {
        if (other.data_) Assign(other.data_, other.size_);
    }
    
    String(String&& other) noexcept 
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    ~String() {
        if (data_) delete[] data_;
    }
    
    String& operator=(const String& other) {
        if (this != &other) {
            String tmp(other);
            Swap(tmp);
        }
        return *this;
    }
    
    String& operator=(String&& other) noexcept {
        if (this != &other) {
            if (data_) delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
    
    String& operator=(std::string_view sv) { Assign(sv.data(), sv.size()); return *this; }
    String& operator=(const char* str) { Assign(str, str ? strlen(str) : 0); return *this; }
    
    reference operator[](size_type index) { return data_[index]; }
    const_reference operator[](size_type index) const { return data_[index]; }
    
    const_reference at(size_type index) const {
        if (index >= size_) throw std::out_of_range("String::at");
        return data_[index];
    }
    
    reference at(size_type index) {
        if (index >= size_) throw std::out_of_range("String::at");
        return data_[index];
    }
    
    const_reference front() const { return data_[0]; }
    reference front() { return data_[0]; }
    const_reference back() const { return data_[size_ - 1]; }
    reference back() { return data_[size_ - 1]; }
    
    const char* c_str() const { return data_ ? data_ : ""; }
    const char* data() const { return data_ ? data_ : ""; }
    const char* begin() const { return data_; }
    const char* end() const { return data_ + size_; }
    
    size_type size() const { return size_; }
    size_type length() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    String& operator+=(const String& other);
    String& operator+=(const char* str);
    String& operator+=(char c);
    
    String operator+(const String& other) const;
    String operator+(const char* str) const;
    
    bool operator==(const String& other) const {
        if (size_ != other.size_) return false;
        if (size_ == 0) return true;
        return memcmp(data_, other.data_, size_) == 0;
    }
    
    bool operator==(std::string_view sv) const {
        if (size_ != sv.size()) return false;
        if (size_ == 0) return true;
        return memcmp(data_, sv.data(), size_) == 0;
    }
    
    bool operator==(const char* str) const {
        if (!str) return size_ == 0;
        size_type len = strlen(str);
        if (size_ != len) return false;
        if (size_ == 0) return true;
        return memcmp(data_, str, len) == 0;
    }
    
    String substr(size_type pos = 0, size_type count = npos) const;
    size_type Find(char c, size_type pos = 0) const;
    size_type RFind(char c, size_type pos = npos) const;
    String Trim() const;
    Array<String> Split(char delimiter) const;
    int ToInt() const;
    double ToDouble() const;
    size_type Hash() const;
    
    void Swap(String& other) noexcept;
    void Assign(const char* str, size_type len);
    void Reserve(size_type new_cap);
    
    static constexpr size_type npos = size_type(-1);
    
    char* data_;
    size_type size_;
    size_type capacity_;
};

class StringBuilder {
public:
    StringBuilder();
    StringBuilder& Append(const String& str);
    StringBuilder& Append(const char* str);
    StringBuilder& Append(char c);
    String Finish();
    
private:
    Array<char> buffer_;
};

String operator+(const char* lhs, const String& rhs);

} // namespace zq
