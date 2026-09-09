#pragma once

#include <cstddef>
#include <cstdint>

namespace zq::memory {

// Base allocator interface
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* Allocate(size_t size, size_t alignment = 8) = 0;
    virtual void Deallocate(void* ptr) = 0;
};

// Stack allocator - LIFO, no individual deallocation
class StackAllocator : public IAllocator {
public:
    explicit StackAllocator(size_t size);
    ~StackAllocator() override;
    
    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;
    
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
    
    void Reset();
    size_t Used() const;
    size_t Capacity() const;
    
private:
    char* buffer_;
    size_t capacity_;
    size_t used_;
    size_t alignment_;
};

// Pool allocator - fixed-size blocks
class PoolAllocator : public IAllocator {
public:
    PoolAllocator(size_t block_size, size_t block_count);
    ~PoolAllocator() override;
    
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
    
    size_t BlockSize() const;
    size_t BlockCount() const;
    size_t Used() const;
    
private:
    struct Block {
        Block* next;
    };
    
    char* pool_;
    Block* free_list_;
    size_t block_size_;
    size_t block_count_;
    size_t used_;
};

// Default allocator (uses global new/delete)
class DefaultAllocator : public IAllocator {
public:
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
};

// Allocator with tracking (debug)
class TrackedAllocator : public IAllocator {
public:
    explicit TrackedAllocator(IAllocator* underlying);
    
    void* Allocate(size_t size, size_t alignment = 8) override;
    void Deallocate(void* ptr) override;
    
    void ReportLeaks();
    size_t TotalAllocated() const;
    size_t AllocationCount() const;
    
private:
    IAllocator* underlying_;
    struct AllocRecord {
        void* ptr;
        size_t size;
        AllocRecord* next;
    };
    AllocRecord* records_;
    size_t total_allocated_;
    size_t allocation_count_;
};

} // namespace zq::memory
