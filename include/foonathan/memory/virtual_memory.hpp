// Copyright (C) 2015 Jonathan Müller <jonathanmueller.dev@gmail.com>
// This file is subject to the license terms in the LICENSE file
// found in the top-level directory of this distribution.

#ifndef FOONATHAN_MEMORY_VIRTUAL_MEMORY_HPP_INCLUDED
#define FOONATHAN_MEMORY_VIRTUAL_MEMORY_HPP_INCLUDED

/// \file
/// Virtual memory api and (low-level) allocator classes.

#include <cstddef>
#include <type_traits>

#include "detail/utility.hpp"
#include "config.hpp"
#include "error.hpp"

namespace foonathan { namespace memory
{
#if FOONATHAN_MEMORY_DEBUG_LEAK_CHECK
    namespace detail
    {
        static struct virtual_memory_allocator_leak_checker_initializer_t
        {
            virtual_memory_allocator_leak_checker_initializer_t() FOONATHAN_NOEXCEPT;
            ~virtual_memory_allocator_leak_checker_initializer_t() FOONATHAN_NOEXCEPT;
        } virtual_memory_allocator_leak_checker_initializer;
    } // namespace detail
#endif

    /// The page size of the virtual memory.
    /// All virtual memory allocations must be multiple of this size.
    /// It is usually 4KiB.
    /// \ingroup memory
    extern const std::size_t virtual_memory_page_size;

    /// Reserves virtual memory.
    /// \effects Reserves the given number of pages.
    /// Each page is \ref virtual_memory_page_size big.
    /// \returns The address of the first reserved page,
    /// or \c nullptr in case of error.
    /// \note The memory may not be used, it must first be commited.
    /// \ingroup memory
    void* virtual_memory_reserve(std::size_t no_pages) FOONATHAN_NOEXCEPT;

    /// Releases reserved virtual memory.
    /// \effects Returns previously reserved pages to the system.
    /// \requires \c pages must come from a previous call to \ref virtual_memory_reserve with the same \c calc_no_pages,
    /// it must not be \c nullptr.
    /// \ingroup memory
    void virtual_memory_release(void *pages, std::size_t no_pages) FOONATHAN_NOEXCEPT;

    /// Commits reserved virtual memory.
    /// \effects Marks \c calc_no_pages pages starting at the given address available for use.
    /// \returns The beginning of the committed area, i.e. \c memory, or \c nullptr in case of error.
    /// \requires The memory must be previously reserved.
    /// \ingroup memory
    void* virtual_memory_commit(void *memory, std::size_t no_pages) FOONATHAN_NOEXCEPT;

    /// Decommits commited virtual memory.
    /// \effects Puts commited memory back in the reserved state.
    /// \requires \c memory must come from a previous call to \ref virtual_memory_commit with the same \c calc_no_pages
    /// it must not be \c nullptr.
    /// \ingroup memory
    void virtual_memory_decommit(void *memory, std::size_t no_pages) FOONATHAN_NOEXCEPT;

    /// A stateless \concept{concept_rawallocator,RawAllocator} that allocates memory using the virtual memory allocation functions.
    /// It does not prereserve any memory and will always reserve and commit combined.
    /// \ingroup memory
    class virtual_memory_allocator
    {
    public:
        using is_stateful = std::false_type;

        virtual_memory_allocator() FOONATHAN_NOEXCEPT = default;
        virtual_memory_allocator(virtual_memory_allocator&&) FOONATHAN_NOEXCEPT {}
        ~virtual_memory_allocator() FOONATHAN_NOEXCEPT = default;

        virtual_memory_allocator& operator=(virtual_memory_allocator &&) FOONATHAN_NOEXCEPT
        {
            return *this;
        }

        /// \effects A \concept{concept_rawallocator,RawAllocator} allocation function.
        /// It uses \ref virtual_memory_reserve followed by \ref virtual_memory_commit for the allocation.
        /// The number of pages allocated will be the minimum to hold \c size continuous bytes,
        /// i.e. \c size will be rounded up to the next multiple.
        /// If debug fences are activated, one additional page before and after the memory will be allocated.
        /// \returns A pointer to a \concept{concept_node,node}, it will never be \c nullptr.
        /// It will always be aligned on a fence boundary, regardless of the alignment parameter.
        /// \throws An exception of type \ref out_of_memory or whatever is thrown by its handler if the allocation fails.
        void* allocate_node(std::size_t size, std::size_t alignment);

        /// \effects A \concept{concept_rawallocator,RawAllocator} deallocation function.
        /// It calls \ref virtual_memory_decommit followed by \ref virtual_memory_release for the deallocation.
        void deallocate_node(void *node, std::size_t size, std::size_t alignment) FOONATHAN_NOEXCEPT;

        /// \returns The maximum node size by returning the maximum value.
        std::size_t max_node_size() const FOONATHAN_NOEXCEPT;

        /// \returns The maximum alignment which is the same as the \ref virtual_memory_page_size.
        std::size_t max_alignment() const FOONATHAN_NOEXCEPT;
    };

    struct memory_block;
    struct allocator_info;

    /// A \concept{concept_blockallocator,BlockAllocator} that reserves virtual memory and commits it part by part.
    /// It is similar to \ref memory_stack but does not support growing and uses virtual memory,
    /// also meant for big blocks not small allocations.
    /// \ingroup memory
    class virtual_block_allocator
    {
    public:
        /// \effects Creates it giving it the block size and the total number of blocks it can allocate.
        /// It reserves enough virtual memory for <tt>block_size * no_blocks</tt>.
        /// \requires \c block_size must be non-zero and a multiple of the \ref virtual_memory_page_size.
        /// \c no_blocks must be bigger than \c 1.
        /// \throws \ref out_of_memory if it cannot reserve the virtual memory.
        explicit virtual_block_allocator(std::size_t block_size, std::size_t no_blocks);

        /// \effects Releases the reserved virtual memory.
        ~virtual_block_allocator() FOONATHAN_NOEXCEPT;

        /// @{
        /// \effects Moves the block allocator, it transfers ownership over the reserved area.
        /// This does not invalidate any memory blocks.
        virtual_block_allocator(virtual_block_allocator &&other) FOONATHAN_NOEXCEPT
        : cur_(other.cur_), end_(other.end_),
          block_size_(other.block_size_)
        {
            other.cur_ = other.end_ = nullptr;
            other.block_size_ = 0;
        }

        virtual_block_allocator& operator=(virtual_block_allocator &&other) FOONATHAN_NOEXCEPT
        {
            virtual_block_allocator tmp(detail::move(other));
            swap(*this, tmp);
            return *this;
        }
        /// @}

        /// \effects Swaps the ownership over the reserved memory.
        /// This does not invalidate any memory blocks.
        friend void swap(virtual_block_allocator &a, virtual_block_allocator &b) FOONATHAN_NOEXCEPT
        {
            detail::adl_swap(a.cur_, b.cur_);
            detail::adl_swap(a.end_, b.end_);
            detail::adl_swap(a.block_size_, b.block_size_);
        }

        /// \effects Allocates a new memory block by committing the next \ref next_block_size() number of bytes.
        /// \returns The \ref memory_block committed.
        /// \throws \ref out_of_memory if it cannot commit the memory or the \ref capacity() is exhausted.
        memory_block allocate_block();

        /// \effects Deallocates the last allocated memory block by decommitting it.
        /// This block will be returned again on the next call to \ref allocate_block().
        /// \requires \c block must be the current top block of the memory,
        /// this is guaranteed by \ref memory_arena.
        void deallocate_block(memory_block block) FOONATHAN_NOEXCEPT;

        /// \returns The next block size, this is the block size of the constructor.
        std::size_t next_block_size() const FOONATHAN_NOEXCEPT
        {
            return block_size_;
        }

        /// \returns The number of blocks that can be committed until it runs out of memory.
        std::size_t capacity_left() const FOONATHAN_NOEXCEPT
        {
            return (end_ - cur_) / block_size_;
        }

    private:
        allocator_info info() FOONATHAN_NOEXCEPT;

        char *cur_, *end_;
        std::size_t block_size_;
    };
}} // namespace foonathan::memory

#endif //FOONATHAN_MEMORY_VIRTUAL_MEMORY_HPP_INCLUDED
