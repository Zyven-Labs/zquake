#pragma once

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <functional>
#include "core/container/array.hpp"

namespace zq {

// Hash function for String keys
inline size_t StringHash(const String& str) {
    size_t hash = 5381;
    for (size_t i = 0; i < str.size(); i++) {
        hash = ((hash << 5) + hash) + str[i];
    }
    return hash;
}

template<typename K, typename V>
class HashMap {
public:
    using key_type = K;
    using mapped_type = V;
    using size_type = size_t;
    
    struct Entry {
        K key;
        V value;
        bool occupied : 1;
        bool deleted : 1;
        Entry() : key{}, value{}, occupied(false), deleted(false) {}
    };
    
    HashMap() : entries_(64), size_(0) { ConstructAll(); }
    
    void Reserve(size_type capacity) {
        size_type new_capacity = 1;
        while (new_capacity < capacity) new_capacity *= 2;
        entries_.Reserve(new_capacity);
        ConstructAll();
    }
    
    void Clear() {
        for (size_type i = 0; i < entries_.capacity(); i++) {
            if (entries_[i].occupied) {
                entries_[i].key.~K();
                entries_[i].value.~V();
                entries_[i].occupied = false;
                entries_[i].deleted = false;
            }
        }
        size_ = 0;
    }
    
    bool Insert(const K& key, V value) {
        if (Size() * 2 > Capacity()) Resize(Capacity() * 2);
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied) {
            if (!entries_[idx].deleted && entries_[idx].key == key) { entries_[idx].value = value; return false; }
            idx = (idx + 1) % cap;
            if (idx == start) return false;
        }
        entries_[idx].key = key;
        entries_[idx].value = value;
        entries_[idx].occupied = true;
        entries_[idx].deleted = false;
        size_++;
        return true;
    }
    
    V& operator[](const K& key) {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return entries_[idx].value;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        Insert(key, V{});
        return operator[](key);
    }
    
    const V& Get(const K& key, const V& fallback = V{}) const {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return entries_[idx].value;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return fallback;
    }
    
    bool Contains(const K& key) const {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return true;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return false;
    }
    
    bool Remove(const K& key) {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied) {
            if (!entries_[idx].deleted && entries_[idx].key == key) {
                entries_[idx].deleted = true; size_--; return true;
            }
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return false;
    }
    
    size_type Size() const { return size_; }
    size_type Capacity() const { return entries_.capacity(); }
    bool Empty() const { return size_ == 0; }
    
private:
    size_type HashKey(const K& key) const { return static_cast<size_type>(std::hash<K>()(key)); }
    
    void ConstructAll() {
        for (size_type i = 0; i < entries_.capacity(); i++) {
            new (&entries_[i]) Entry();
        }
    }
    
    void Resize(size_type new_capacity) {
        Array<Entry> old_entries(std::move(entries_));
        entries_.Reserve(new_capacity);
        ConstructAll();
        for (size_type i = 0; i < old_entries.size(); i++) {
            if (old_entries[i].occupied && !old_entries[i].deleted) {
                Insert(old_entries[i].key, old_entries[i].value);
            }
        }
    }
    
    Array<Entry> entries_;
    size_type size_;
};

// String-keyed HashMap using our custom hash
template<typename V>
class HashMap<String, V> {
public:
    using key_type = String;
    using mapped_type = V;
    using size_type = size_t;
    
    struct Entry {
        String key;
        V value;
        bool occupied : 1;
        bool deleted : 1;
        Entry() : key{}, value{}, occupied(false), deleted(false) {}
    };
    
    HashMap() : entries_(64), size_(0) { ConstructAll(); }
    
    void Reserve(size_type) { entries_.Reserve(entries_.capacity() * 2); ConstructAll(); }
    
    void Clear() {
        for (size_type i = 0; i < entries_.capacity(); i++) {
            if (entries_[i].occupied) {
                entries_[i].key.~String();
                entries_[i].value.~V();
                entries_[i].occupied = false;
                entries_[i].deleted = false;
            }
        }
        size_ = 0;
    }
    
    bool Insert(const String& key, V value) {
        if (Size() * 2 > Capacity()) Resize(Capacity() * 2);
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied) {
            if (!entries_[idx].deleted && entries_[idx].key == key) { entries_[idx].value = value; return false; }
            idx = (idx + 1) % cap;
            if (idx == start) return false;
        }
        entries_[idx].key = key;
        entries_[idx].value = value;
        entries_[idx].occupied = true;
        entries_[idx].deleted = false;
        size_++;
        return true;
    }
    
    V& operator[](const String& key) {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return entries_[idx].value;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        Insert(key, V{});
        return operator[](key);
    }
    
    const V& Get(const String& key, const V& fallback = V{}) const {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return entries_[idx].value;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return fallback;
    }
    
    bool Contains(const String& key) const {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied && !entries_[idx].deleted) {
            if (entries_[idx].key == key) return true;
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return false;
    }
    
    bool Remove(const String& key) {
        size_type cap = Capacity();
        size_type idx = HashKey(key) % cap;
        size_type start = idx;
        while (entries_[idx].occupied) {
            if (!entries_[idx].deleted && entries_[idx].key == key) {
                entries_[idx].deleted = true; size_--; return true;
            }
            idx = (idx + 1) % cap;
            if (idx == start) break;
        }
        return false;
    }
    
    size_type Size() const { return size_; }
    size_type Capacity() const { return entries_.capacity(); }
    bool Empty() const { return size_ == 0; }
    
private:
    size_type HashKey(const String& key) const { return StringHash(key); }
    
    void ConstructAll() {
        for (size_type i = 0; i < entries_.capacity(); i++) {
            new (&entries_[i]) Entry();
        }
    }
    
    void Resize(size_type new_capacity) {
        Array<Entry> old_entries(std::move(entries_));
        entries_.Reserve(new_capacity);
        ConstructAll();
        for (size_type i = 0; i < old_entries.size(); i++) {
            if (old_entries[i].occupied && !old_entries[i].deleted) {
                Insert(old_entries[i].key, old_entries[i].value);
            }
        }
    }
    
    Array<Entry> entries_;
    size_type size_;
};

} // namespace zq
