Cache Simulator README.
-> Features
    Supports Direct Mapped, N-Way Set Associative, and Fully Associative caches
    Replacement policies: LRU, LFU, FIFO, Random
    Write policies: Write-Through / Write-Back, Write-Allocate / No-Write-Allocate
    Detailed statistics:
    Hits, misses, hit/miss rate
    Compulsory, capacity, and conflict misses
    Reads, writes, write-backs
    Memory traffic & AMAT
    Approximate cycle count and simulation time
    Multiple memory access patterns: sequential, random, localized random, strided, temporal locality, mixed
    Interactive menu for configuration
    Export results to CSV (cache_results.csv)
    Built-in quick test and comparison mode for multiple configurations

->Notes
   Address space is simulated; synthetic traces are generated internally
   Input validation ensures powers-of-two sizes and reasonable ranges
   Timing model uses simple fixed hit and memory access times
   Large access counts increase runtime—adjust num_accesses for quick tests

->License
    MIT recommended — add a LICENSE file if publishing publicly.
