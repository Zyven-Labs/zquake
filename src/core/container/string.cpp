#include "core/container/string.hpp"
#include <cstdlib>
#include <cstdio>

namespace zq {

String& String::operator+=(const String& other) {
    if (other.size_ == 0) return *this;
    size_type new_size = size_ + other.size_;
    if (new_size > capacity_) {
        char* new_data = new char[new_size + 1];
        if (size_ > 0 && data_) memcpy(new_data, data_, size_);
        memcpy(new_data + size_, other.data_, other.size_);
        if (data_) delete[] data_;
        data_ = new_data;
        capacity_ = new_size;
    } else {
        memcpy(data_ + size_, other.data_, other.size_);
    }
    size_ = new_size;
    data_[size_] = '\0';
    return *this;
}

String& String::operator+=(const char* str) {
    size_type len = strlen(str);
    if (len == 0) return *this;
    size_type new_size = size_ + len;
    if (new_size > capacity_) {
        char* new_data = new char[new_size + 1];
        if (size_ > 0 && data_) memcpy(new_data, data_, size_);
        memcpy(new_data + size_, str, len);
        if (data_) delete[] data_;
        data_ = new_data;
        capacity_ = new_size;
    } else {
        memcpy(data_ + size_, str, len);
    }
    size_ = new_size;
    data_[size_] = '\0';
    return *this;
}

String& String::operator+=(char c) {
    if (size_ + 1 > capacity_) {
        size_type new_capacity = capacity_ == 0 ? 16 : capacity_ * 2;
        char* new_data = new char[new_capacity + 1];
        if (data_) memcpy(new_data, data_, size_);
        if (data_) delete[] data_;
        data_ = new_data;
        capacity_ = new_capacity;
    }
    data_[size_++] = c;
    data_[size_] = '\0';
    return *this;
}

String String::operator+(const String& other) const {
    String result;
    result.Reserve(size_ + other.size_);
    if (size_ > 0 && data_) memcpy(result.data_, data_, size_);
    memcpy(result.data_ + size_, other.data_, other.size_);
    result.size_ = size_ + other.size_;
    result.data_[result.size_] = '\0';
    return result;
}

String String::operator+(const char* str) const {
    size_type len = strlen(str);
    String result;
    result.Reserve(size_ + len);
    if (size_ > 0 && data_) memcpy(result.data_, data_, size_);
    memcpy(result.data_ + size_, str, len);
    result.size_ = size_ + len;
    result.data_[result.size_] = '\0';
    return result;
}

String String::substr(size_type pos, size_type count) const {
    if (pos >= size_) return String();
    if (count == npos || pos + count > size_) count = size_ - pos;
    return String(data_ + pos, count);
}

size_t String::Find(char c, size_type pos) const {
    for (size_type i = pos; i < size_; i++) {
        if (data_[i] == c) return i;
    }
    return npos;
}

size_t String::RFind(char c, size_type pos) const {
    if (pos >= size_) pos = size_ - 1;
    for (size_type i = pos; i != npos; i--) {
        if (data_[i] == c) return i;
    }
    return npos;
}

String String::Trim() const {
    size_type start = 0, end = size_;
    while (start < size_ && (data_[start] == ' ' || data_[start] == '\t' || data_[start] == '\n' || data_[start] == '\r')) start++;
    while (end > start && (data_[end - 1] == ' ' || data_[end - 1] == '\t' || data_[end - 1] == '\n' || data_[end - 1] == '\r')) end--;
    return String(data_ + start, end - start);
}

Array<String> String::Split(char delimiter) const {
    Array<String> result;
    size_type start = 0;
    for (size_type i = 0; i < size_; i++) {
        if (data_[i] == delimiter) {
            result.PushBack(String(data_ + start, i - start));
            start = i + 1;
        }
    }
    result.PushBack(String(data_ + start, size_ - start));
    return result;
}

int String::ToInt() const {
    if (size_ == 0) return 0;
    int result = 0;
    int sign = 1;
    size_type start = 0;
    if (data_[0] == '-') { sign = -1; start = 1; }
    else if (data_[0] == '+') start = 1;
    for (size_type i = start; i < size_; i++) {
        if (data_[i] >= '0' && data_[i] <= '9') result = result * 10 + (data_[i] - '0');
        else break;
    }
    return result * sign;
}

double String::ToDouble() const {
    if (size_ == 0) return 0.0;
    char* buf = new char[size_ + 1];
    if (data_) memcpy(buf, data_, size_);
    buf[size_] = '\0';
    double result = strtod(buf, nullptr);
    delete[] buf;
    return result;
}

size_t String::Hash() const {
    size_t hash = 5381;
    for (size_type i = 0; i < size_; i++) hash = ((hash << 5) + hash) + data_[i];
    return hash;
}

void String::Swap(String& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
}

void String::Assign(const char* str, size_type len) {
    if (data_) delete[] data_;
    if (len == 0) { data_ = nullptr; size_ = 0; capacity_ = 0; return; }
    data_ = new char[len + 1];
    memcpy(data_, str, len);
    data_[len] = '\0';
    size_ = len;
    capacity_ = len;
}

void String::Reserve(size_type new_cap) {
    if (new_cap <= capacity_) return;
    char* new_data = new char[new_cap + 1];
    if (size_ > 0 && data_) memcpy(new_data, data_, size_);
    if (data_) delete[] data_;
    data_ = new_data;
    capacity_ = new_cap;
    data_[size_] = '\0';
}

StringBuilder::StringBuilder() : buffer_(128) {}

StringBuilder& StringBuilder::Append(const String& str) {
    for (size_t i = 0; i < str.size_; i++) buffer_.PushBack(str.data_[i]);
    return *this;
}

StringBuilder& StringBuilder::Append(const char* str) {
    while (*str) buffer_.PushBack(*str++);
    return *this;
}

StringBuilder& StringBuilder::Append(char c) {
    buffer_.PushBack(c);
    return *this;
}

String StringBuilder::Finish() {
    String result;
    result.Assign(buffer_.data(), buffer_.size());
    buffer_.Clear();
    return result;
}

String operator+(const char* lhs, const String& rhs) {
    size_t len = strlen(lhs);
    String result;
    result.Reserve(len + rhs.size_);
    memcpy(result.data_, lhs, len);
    memcpy(result.data_ + len, rhs.data_, rhs.size_);
    result.size_ = len + rhs.size_;
    result.data_[result.size_] = '\0';
    return result;
}

} // namespace zq
