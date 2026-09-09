#include "core/memory/allocator.hpp"
#include <algorithm>
#include <cstring>

namespace zq::memory {

// Align size up to alignment boundary
static constexpr size_t AlignUp(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

StackAllocator::StackAllocator(size_t size)
    : buffer_(nullptr), capacity_(size), used_(0), alignment_(8) {
    buffer_ = static_cast<char*>(::operator new(size));
}

StackAllocator::~StackAllocator() {
    ::operator delete(buffer_);
}

void* StackAllocator::Allocate(size_t size, size_t alignment) {
    size_t aligned_used = AlignUp(used_, alignment);
    if (aligned_used + size > capacity_) {
        return nullptr; // Out of memory
    }
    void* ptr = buffer_ + aligned_used;
    used_ = aligned_used + size;
    return ptr;
}

void StackAllocator::Deallocate(void* ptr) {
    // Stack allocator ignores individual deallocations
    // Reset() must be used to reclaim memory
}

void StackAllocator::Reset() {
    used_ = 0;
}

size_t StackAllocator::Used() const {
    return used_;
}

size_t StackAllocator::Capacity() const {
    return capacity_;
}

PoolAllocator::PoolAllocator(size_t block_size, size_t block_count)
    : pool_(nullptr), free_list_(nullptr), 
      block_size_(AlignUp(block_size, 8)), block_count_(block_count), used_(0) {
    // Allocate the entire pool
    pool_ = static_cast<char*>(::operator new(block_size_ * block_count_));
    
    // Build free list
    char* current = pool_;
    for (size_t i = 0; i < block_count_; i++) {
        Block* block = reinterpret_cast<Block*>(current);
        block->next = free_list_;
        free_list_ = block;
        current += block_size_;
    }
}

PoolAllocator::~PoolAllocator() {
    ::operator delete(pool_);
}

void* PoolAllocator::Allocate(size_t size, size_t alignment) {
    if (size > block_size_ || !free_list_) {
        return nullptr; // Block too large or pool exhausted
    }
    Block* block = free_list_;
    free_list_ = block->next;
    used_++;
    return static_cast<void*>(block);
}

void PoolAllocator::Deallocate(void* ptr) {
    if (!ptr) return;
    Block* block = static_cast<Block*>(ptr);
    block->next = free_list_;
    free_list_ = block;
    used_--;
}

size_t PoolAllocator::BlockSize() const { return block_size_; }
size_t PoolAllocator::BlockCount() const { return block_count_; }
size_t PoolAllocator::Used() const { return used_; }

void* DefaultAllocator::Allocate(size_t size, size_t alignment) {
    // Simple allocation - assume 8-byte alignment
    if (size == 0) return nullptr;
    
    // Align the allocation
    size_t aligned_size = AlignUp(size, alignment);
    void* ptr = ::operator new(aligned_size);
    return ptr;
}

void DefaultAllocator::Deallocate(void* ptr) {
    if (ptr) {
        ::operator delete(ptr);
    }
}

TrackedAllocator::TrackedAllocator(IAllocator* underlying)
    : underlying_(underlying), records_(nullptr), 
      total_allocated_(0), allocation_count_(0) {}

void* TrackedAllocator::Allocate(size_t size, size_t alignment) {
    void* ptr = underlying_->Allocate(size, alignment);
    if (ptr) {
        auto* record = new AllocRecord{ptr, size, records_};
        records_ = record;
        total_allocated_ += size;
        allocation_count_++;
    }
    return ptr;
}

void TrackedAllocator::Deallocate(void* ptr) {
    if (!ptr) return;
    underlying_->Deallocate(ptr);
    
    // Remove from records
    AllocRecord** current = &records_;
    while (*current) {
        if ((*current)->ptr == ptr) {
            AllocRecord* to_delete = *current;
            *current = to_delete->next;
            delete to_delete;
            total_allocated_ -= to_delete->size;
            allocation_count_--;
            return;
        }
        current = &(*current)->next;
    }
}

void TrackedAllocator::ReportLeaks() {
    for (auto* record = records_; record; record = record->next) {
        // Would use logging system - print for now
    }
}

size_t TrackedAllocator::TotalAllocated() const {
    return total_allocated_;
}

size_t TrackedAllocator::AllocationCount() const {
    return allocation_count_;
}

} // namespace zq::memory
