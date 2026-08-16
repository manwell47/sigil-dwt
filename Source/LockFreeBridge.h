#pragma once

#include <vector>
#include <atomic>
#include <array>
#include <cassert>

namespace steganography
{

/**
 * @class LockFreeBridge
 * @brief Triple-buffer for Lock-Free communication between ML Worker and Audio DSP.
 * 
 * Pre-allocates 3 buffers of the given size. 
 * The worker thread can write to a buffer and swap it atomically.
 * The audio thread reads from the latest available complete buffer without blocking.
 * 100% mutex-free.
 */
class LockFreeBridge
{
public:
    LockFreeBridge() 
        : readIndex(0), writeIndex(1), dirtyIndex(2), hasNewData(false)
    {
    }

    /**
     * @brief Allocates the 3 underlying buffers. Must be called BEFORE audio starts.
     */
    void allocate(size_t bufferSize)
    {
        for (auto& buf : buffers)
        {
            buf.assign(bufferSize, 0.0f);
        }
    }

    /**
     * @brief Get a pointer to the current writing buffer. (Called by Worker Thread)
     */
    std::vector<float>& getWriteBuffer()
    {
        return buffers[writeIndex.load(std::memory_order_relaxed)];
    }

    /**
     * @brief Swaps the write buffer with the dirty buffer to make it available for reading. (Called by Worker Thread)
     */
    void swapWriteBuffer()
    {
        // Swap writeIndex and dirtyIndex atomically
        int currentWrite = writeIndex.load(std::memory_order_relaxed);
        int currentDirty = dirtyIndex.exchange(currentWrite, std::memory_order_acq_rel);
        
        writeIndex.store(currentDirty, std::memory_order_relaxed);
        hasNewData.store(true, std::memory_order_release);
    }

    /**
     * @brief Called by the Audio Thread to grab the newest data if available.
     * @return Reference to the latest valid buffer.
     */
    const std::vector<float>& getReadBuffer()
    {
        if (hasNewData.exchange(false, std::memory_order_acquire))
        {
            // Swap readIndex and dirtyIndex atomically
            int currentRead = readIndex.load(std::memory_order_relaxed);
            int currentDirty = dirtyIndex.exchange(currentRead, std::memory_order_acq_rel);
            
            readIndex.store(currentDirty, std::memory_order_relaxed);
        }
        
        return buffers[readIndex.load(std::memory_order_relaxed)];
    }

private:
    std::array<std::vector<float>, 3> buffers;
    
    // Indices for the 3 buffers: [0, 1, 2]
    std::atomic<int> readIndex;
    std::atomic<int> writeIndex;
    std::atomic<int> dirtyIndex;
    
    std::atomic<bool> hasNewData;
};

} // namespace steganography
