
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <deque>
#include <queue>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <chrono>

using namespace std;

// ==================== Constants and Enums ==================== 

#define DRAM_SIZE (64*1024*1024)
#define L1_HIT_TIME 1
#define L2_HIT_TIME 10
#define DRAM_ACCESS_TIME 100

enum CacheType { DIRECT_MAPPED, SET_ASSOCIATIVE, FULLY_ASSOCIATIVE };
enum ReplacementPolicy { LRU, LFU, FIFO, RANDOM };
enum WritePolicy { WRITE_THROUGH, WRITE_BACK };
enum WriteAllocatePolicy { WRITE_ALLOCATE, NO_WRITE_ALLOCATE };
enum AccessType { READ, WRITE };
// ==================== Utility Functions ====================

class Utils {
public:
    static bool isPowerOfTwo(int n) {
        return n > 0 && (n & (n - 1)) == 0;
    }
    
    static unsigned int rand_() {
        static unsigned int m_w = 0xABABAB55;
        static unsigned int m_z = 0x05080902;
        m_z = 36969 * (m_z & 65535) + (m_z >> 16);
        m_w = 18000 * (m_w & 65535) + (m_w >> 16);
        return (m_z << 16) + m_w;
    }
    
    static string formatBytes(long long bytes) {
        if (bytes >= 1024*1024) return to_string(bytes/(1024*1024)) + " MB";
        if (bytes >= 1024) return to_string(bytes/1024) + " KB";
        return to_string(bytes) + " B";
    }
};

// ==================== Address Information ====================

struct AddressInfo {
    int offset_bits;
    int index_bits;
    int tag_bits;
    unsigned int offset_mask;
    unsigned int index_mask;
    unsigned int tag_mask;
    
    AddressInfo(int block_size, int num_blocks) {
        offset_bits = log2(block_size);
        index_bits = log2(num_blocks);
        tag_bits = 32 - offset_bits - index_bits;
        
        offset_mask = (1 << offset_bits) - 1;
        index_mask = (1 << index_bits) - 1;
        tag_mask = (1 << tag_bits) - 1;
    }
    
    void extract(unsigned int addr, unsigned int& tag, unsigned int& index, unsigned int& offset) {
        offset = addr & offset_mask;
        index = (addr >> offset_bits) & index_mask;
        tag = (addr >> (offset_bits + index_bits)) & tag_mask;
    }
};

// ==================== Cache Line ====================

class CacheLine {
public:
    unsigned int tag;
    bool valid;
    bool dirty;
    int lru_counter;
    int frequency;
    int fifo_timestamp;
    
    CacheLine() : tag(0), valid(false), dirty(false), 
                  lru_counter(0), frequency(0), fifo_timestamp(0) {}
    
    void reset() {
        valid = false;
        dirty = false;
        lru_counter = 0;
        frequency = 0;
        fifo_timestamp = 0;
    }
};

// ==================== Statistics ====================

struct CacheStats {
    long long total_accesses;
    long long hits;
    long long misses;
    long long reads;
    long long writes;
    long long write_backs;
    long long compulsory_misses;
    long long capacity_misses;
    long long conflict_misses;
    long long memory_traffic;
    
    double simulation_time;
    
    CacheStats() : total_accesses(0), hits(0), misses(0), reads(0), writes(0),
                   write_backs(0), compulsory_misses(0), capacity_misses(0),
                   conflict_misses(0), memory_traffic(0), simulation_time(0) {}
    
    double getHitRate() const { 
        return total_accesses > 0 ? (double)hits / total_accesses * 100 : 0; 
    }
    
    double getMissRate() const { 
        return total_accesses > 0 ? (double)misses / total_accesses * 100 : 0; 
    }
    
    double getAMAT(double hit_time, double miss_penalty) const {
        return hit_time + (getMissRate() / 100.0) * miss_penalty;
    }
    
    void display(const string& cache_name, int cache_size, int block_size, 
                 int associativity = 0) const {
        cout << "\n==================================================================\n";
        cout << "=           " << cache_name << " - Simulation Results               \n";
        cout << "=====================================================================\n";
        
        cout << "= Configuration:                                             \n";
        cout << "=   Cache Size: " << setw(6) << cache_size << " KB" 
             << "                                      \n";
        cout << "=   Block Size: " << setw(6) << block_size << " bytes" 
             << "                                   \n";
        if (associativity > 0) {
            cout << "=   Associativity: " << setw(4) << associativity 
                 << "-way                                    \n";
        }
        
        cout << "==============================================================\n";
        cout << "= Access Statistics:                                         \n";
        cout << "=   Total Accesses: " << setw(12) << total_accesses 
             << "                              \n";
        cout << "=   Reads:          " << setw(12) << reads 
             << "  (" << setw(5) << fixed << setprecision(1) 
             << (total_accesses > 0 ? (double)reads/total_accesses*100 : 0) 
             << "%)            =\n";
        cout << "=   Writes:         " << setw(12) << writes 
             << "  (" << setw(5) << fixed << setprecision(1) 
             << (total_accesses > 0 ? (double)writes/total_accesses*100 : 0) 
             << "%)            =\n";
        
        cout << "==============================================================\n";
        cout << "= Performance:                                               \n";
        cout << "=   Hits:           " << setw(12) << hits 
             << "  (" << setw(5) << fixed << setprecision(2) 
             << getHitRate() << "%)            =\n";
        cout << "=   Misses:         " << setw(12) << misses 
             << "  (" << setw(5) << fixed << setprecision(2) 
             << getMissRate() << "%)            =\n";
        
        cout << "==============================================================\n";
        cout << "= Miss Breakdown:                                            \n";
        
        double comp_pct = misses > 0 ? (double)compulsory_misses/misses*100 : 0;
        double cap_pct = misses > 0 ? (double)capacity_misses/misses*100 : 0;
        double conf_pct = misses > 0 ? (double)conflict_misses/misses*100 : 0;
        
        cout << "=   Compulsory:     " << setw(12) << compulsory_misses 
             << "  (" << setw(5) << fixed << setprecision(2) 
             << comp_pct << "%)            =\n";
        cout << "=   Capacity:       " << setw(12) << capacity_misses 
             << "  (" << setw(5) << fixed << setprecision(2) 
             << cap_pct << "%)            =\n";
        cout << "=   Conflict:       " << setw(12) << conflict_misses 
             << "  (" << setw(5) << fixed << setprecision(2) 
             << conf_pct << "%)            =\n";
        
        cout << "==============================================================\n";
        cout << "= Memory Traffic:                                            \n";
        cout << "=   Write-backs:    " << setw(12) << write_backs 
             << "                              =\n";
        cout << "=   Memory Traffic: " << setw(12) << memory_traffic 
             << " accesses                     =\n";
        
        cout << "==============================================================\n";
        cout << "= Timing Analysis:                                           \n";
        cout << "=   AMAT:           " << setw(8) << fixed << setprecision(2) 
             << getAMAT(L1_HIT_TIME, DRAM_ACCESS_TIME) 
             << " cycles                            =\n";
        cout << "=   Total Cycles:   " << setw(12) 
             << (long long)(hits * L1_HIT_TIME + misses * DRAM_ACCESS_TIME)
             << "                              =\n";
        cout << "=   Simulation Time:" << setw(10) << fixed << setprecision(3) 
             << simulation_time << " seconds                       =\n";
        
        cout << "================================================================\n\n";
    }
    
    void exportToCSV(const string& filename) const {
        ofstream file(filename, ios::app);
        if (file.is_open()) {
            // Write header if file is empty
            file.seekp(0, ios::end);
            if (file.tellp() == 0) {
                file << "Total,Hits,Misses,HitRate,Compulsory,Capacity,Conflict,"
                     << "Reads,Writes,Writebacks,MemTraffic,AMAT\n";
            }
            
            file << total_accesses << "," << hits << "," << misses << ","
                 << getHitRate() << "," << compulsory_misses << ","
                 << capacity_misses << "," << conflict_misses << ","
                 << reads << "," << writes << "," << write_backs << ","
                 << memory_traffic << "," 
                 << getAMAT(L1_HIT_TIME, DRAM_ACCESS_TIME) << "\n";
            file.close();
        }
    }
};

// ==================== Base Cache Class ====================

class Cache {
protected:
    int cache_size_kb;
    int block_size;
    int num_blocks;
    WritePolicy write_policy;
    WriteAllocatePolicy write_allocate;
    CacheStats stats;
    shared_ptr<AddressInfo> addr_info;
    
    unordered_map<unsigned int, bool> accessed_blocks;  // For tracking compulsory misses
    
public:
    Cache(int size_kb, int blk_size, WritePolicy wp = WRITE_BACK, 
          WriteAllocatePolicy wa = WRITE_ALLOCATE) 
        : cache_size_kb(size_kb), block_size(blk_size), write_policy(wp), 
          write_allocate(wa) {
        
        if (!Utils::isPowerOfTwo(block_size)) {
            throw invalid_argument("Block size must be power of 2");
        }
        if (!Utils::isPowerOfTwo(cache_size_kb)) {
            throw invalid_argument("Cache size must be power of 2");
        }
        
        num_blocks = (cache_size_kb * 1024) / block_size;
        addr_info = make_shared<AddressInfo>(block_size, num_blocks);
    }
    
    virtual ~Cache() {}
    
    virtual bool access(unsigned int addr, AccessType type) = 0;
    
    virtual string getTypeName() const = 0;
    
    void displayStats() const {
        stats.display(getTypeName(), cache_size_kb, block_size);
    }
    
    CacheStats getStats() const { return stats; }
    
    void resetStats() {
        stats = CacheStats();
        accessed_blocks.clear();
    }
    
protected:
    bool isCompulsoryMiss(unsigned int block_addr) {
        if (accessed_blocks.find(block_addr) == accessed_blocks.end()) {
            accessed_blocks[block_addr] = true;
            return true;
        }
        return false;
    }
    
    void handleWriteBack() {
        stats.write_backs++;
        stats.memory_traffic++;
    }
    
    void handleMemoryRead() {
        stats.memory_traffic++;
    }
};

// ==================== Direct Mapped Cache ====================

class DirectMappedCache : public Cache {
private:
    vector<CacheLine> cache_lines;
    
public:
    DirectMappedCache(int size_kb, int blk_size, WritePolicy wp = WRITE_BACK)
        : Cache(size_kb, blk_size, wp, WRITE_ALLOCATE) {
        cache_lines.resize(num_blocks);
    }
    
    bool access(unsigned int addr, AccessType type) override {
        stats.total_accesses++;
        if (type == READ) stats.reads++;
        else stats.writes++;
        
        unsigned int tag, index, offset;
        addr_info->extract(addr, tag, index, offset);
        
        CacheLine& line = cache_lines[index];
        
        // Check for hit
        if (line.valid && line.tag == tag) {
            stats.hits++;
            if (type == WRITE) {
                if (write_policy == WRITE_BACK) {
                    line.dirty = true;
                } else {
                    stats.memory_traffic++;  // Write through
                }
            }
            return true;
        }
        
        // Miss handling
        stats.misses++;
        
        unsigned int block_addr = addr >> addr_info->offset_bits;
        if (isCompulsoryMiss(block_addr)) {
            stats.compulsory_misses++;
        } else if (line.valid) {
            stats.conflict_misses++;
        }
        
        // Write back if dirty
        if (line.valid && line.dirty && write_policy == WRITE_BACK) {
            handleWriteBack();
        }
        
        // Load new line
        if (type == READ || write_allocate == WRITE_ALLOCATE) {
            line.tag = tag;
            line.valid = true;
            line.dirty = (type == WRITE && write_policy == WRITE_BACK);
            handleMemoryRead();
        }
        
        if (type == WRITE && write_policy == WRITE_THROUGH) {
            stats.memory_traffic++;
        }
        
        return false;
    }
    
    string getTypeName() const override {
        return "Direct Mapped Cache";
    }
};

// ==================== Set Associative Cache ====================

class SetAssociativeCache : public Cache {
private:
    int associativity;
    int num_sets;
    vector<vector<CacheLine>> cache_sets;
    ReplacementPolicy replacement_policy;
    int access_counter;
    
public:
    SetAssociativeCache(int size_kb, int blk_size, int assoc, 
                       ReplacementPolicy rp = LRU, WritePolicy wp = WRITE_BACK)
        : Cache(size_kb, blk_size, wp, WRITE_ALLOCATE), 
          associativity(assoc), replacement_policy(rp), access_counter(0) {
        
        if (!Utils::isPowerOfTwo(associativity)) {
            throw invalid_argument("Associativity must be power of 2");
        }
        
        num_sets = num_blocks / associativity;
        cache_sets.resize(num_sets, vector<CacheLine>(associativity));
        
        // update the address info for set associative cache
        addr_info = make_shared<AddressInfo>(block_size, num_sets);
    }
    
    bool access(unsigned int addr, AccessType type) override {
        stats.total_accesses++;
        if (type == READ) stats.reads++;
        else stats.writes++;
        
        access_counter++;
        
        unsigned int tag, index, offset;
        addr_info->extract(addr, tag, index, offset);
        
        vector<CacheLine>& set = cache_sets[index];
        
        // hit check
        for (int i = 0; i < associativity; i++) {
            if (set[i].valid && set[i].tag == tag) {
                stats.hits++;
                updateReplacementInfo(set, i);
                
                if (type == WRITE) {
                    if (write_policy == WRITE_BACK) {
                        set[i].dirty = true;
                    } else {
                        stats.memory_traffic++;
                    }
                }
                return true;
            }
        }
        
        // handling miss
        stats.misses++;
        
        unsigned int block_addr = addr >> addr_info->offset_bits;
        bool is_compulsory = isCompulsoryMiss(block_addr);
        
        // Find victim (scariest line)
        int victim = findVictim(set);
        
        if (!is_compulsory) {
            if (isSetFull(set)) {
                stats.capacity_misses++;
            } else {
                stats.conflict_misses++;
            }
        } else {
            stats.compulsory_misses++;
        }
        
        // Write back if needed
        if (set[victim].valid && set[victim].dirty && write_policy == WRITE_BACK) {
            handleWriteBack();
        }
        
        // Install new line
        if (type == READ || write_allocate == WRITE_ALLOCATE) {
            set[victim].tag = tag;
            set[victim].valid = true;
            set[victim].dirty = (type == WRITE && write_policy == WRITE_BACK);
            set[victim].lru_counter = access_counter;
            set[victim].frequency = 1;
            set[victim].fifo_timestamp = access_counter;
            handleMemoryRead();
        }
        
        if (type == WRITE && write_policy == WRITE_THROUGH) {
            stats.memory_traffic++;
        }
        
        return false;
    }
    
    string getTypeName() const override {
        return to_string(associativity) + "-Way Set Associative Cache (" + 
               getPolicyName() + ")";
    }
    
private:
    bool isSetFull(const vector<CacheLine>& set) {
        for (const auto& line : set) {
            if (!line.valid) return false;
        }
        return true;
    }
    
    int findVictim(const vector<CacheLine>& set) {
        // Find invalid line first
        for (int i = 0; i < associativity; i++) {
            if (!set[i].valid) return i;
        }
        
        // Apply replacement policy
        switch (replacement_policy) {
            case LRU: {
                int victim = 0;
                for (int i = 1; i < associativity; i++) {
                    if (set[i].lru_counter < set[victim].lru_counter) {
                        victim = i;
                    }
                }
                return victim;
            }
            case LFU: {
                int victim = 0;
                for (int i = 1; i < associativity; i++) {
                    if (set[i].frequency < set[victim].frequency) {
                        victim = i;
                    }
                }
                return victim;
            }
            case FIFO: {
                int victim = 0;
                for (int i = 1; i < associativity; i++) {
                    if (set[i].fifo_timestamp < set[victim].fifo_timestamp) {
                        victim = i;
                    }
                }
                return victim;
            }
            case RANDOM:
                return Utils::rand_() % associativity;
        }
        return 0;
    }
    
    void updateReplacementInfo(vector<CacheLine>& set, int way) {
        if (replacement_policy == LRU) {
            set[way].lru_counter = access_counter;
        } else if (replacement_policy == LFU) {
            set[way].frequency++;
        }
        // FIFO doesn't update on hit
    }
    
    string getPolicyName() const {
        switch (replacement_policy) {
            case LRU: return "LRU";
            case LFU: return "LFU";
            case FIFO: return "FIFO";
            case RANDOM: return "Random";
            default: return "Unknown";
        }
    }
};

// ==================== Fully Associative Cache ====================

class FullyAssociativeCache : public Cache {
private:
    vector<CacheLine> cache_lines;
    ReplacementPolicy replacement_policy;
    int access_counter;
    
public:
    FullyAssociativeCache(int size_kb, int blk_size, ReplacementPolicy rp = LRU,
                         WritePolicy wp = WRITE_BACK)
        : Cache(size_kb, blk_size, wp, WRITE_ALLOCATE), 
          replacement_policy(rp), access_counter(0) {
        cache_lines.resize(num_blocks);
        addr_info = make_shared<AddressInfo>(block_size, 1);  // index bits = 0
    }
    
    bool access(unsigned int addr, AccessType type) override {
        stats.total_accesses++;
        if (type == READ) stats.reads++;
        else stats.writes++;
        
        access_counter++;
        
        unsigned int block_addr = addr >> addr_info->offset_bits;
        
        // hit check
        for (int i = 0; i < num_blocks; i++) {
            if (cache_lines[i].valid && cache_lines[i].tag == block_addr) {
                stats.hits++;
                updateReplacementInfo(i);
                
                if (type == WRITE) {
                    if (write_policy == WRITE_BACK) {
                        cache_lines[i].dirty = true;
                    } else {
                        stats.memory_traffic++;
                    }
                }
                return true;
            }
        }
        
        // Miss handling
        stats.misses++;
        
        bool is_compulsory = isCompulsoryMiss(block_addr);
        
        // Find victim
        int victim = findVictim();
        
        if (!is_compulsory) {
            if (isCacheFull()) {
                stats.capacity_misses++;
            }
        } else {
            stats.compulsory_misses++;
        }
        
        // Write back if needed
        if (cache_lines[victim].valid && cache_lines[victim].dirty && 
            write_policy == WRITE_BACK) {
            handleWriteBack();
        }
        
        // Install new line
        if (type == READ || write_allocate == WRITE_ALLOCATE) {
            cache_lines[victim].tag = block_addr;
            cache_lines[victim].valid = true;
            cache_lines[victim].dirty = (type == WRITE && write_policy == WRITE_BACK);
            cache_lines[victim].lru_counter = access_counter;
            cache_lines[victim].frequency = 1;
            cache_lines[victim].fifo_timestamp = access_counter;
            handleMemoryRead();
        }
        
        if (type == WRITE && write_policy == WRITE_THROUGH) {
            stats.memory_traffic++;
        }
        
        return false;
    }
    
    string getTypeName() const override {
        return "Fully Associative Cache (" + getPolicyName() + ")";
    }
    
private:
    bool isCacheFull() {
        for (const auto& line : cache_lines) {
            if (!line.valid) return false;
        }
        return true;
    }
    
    int findVictim() {
        // Find invalid line first
        for (int i = 0; i < num_blocks; i++) {
            if (!cache_lines[i].valid) return i;
        }
        
        // Apply replacement policy
        switch (replacement_policy) {
            case LRU: {
                int victim = 0;
                for (int i = 1; i < num_blocks; i++) {
                    if (cache_lines[i].lru_counter < cache_lines[victim].lru_counter) {
                        victim = i;
                    }
                }
                return victim;
            }
            case LFU: {
                int victim = 0;
                for (int i = 1; i < num_blocks; i++) {
                    if (cache_lines[i].frequency < cache_lines[victim].frequency) {
                        victim = i;
                    }
                }
                return victim;
            }
            case FIFO: {
                int victim = 0;
                for (int i = 1; i < num_blocks; i++) {
                    if (cache_lines[i].fifo_timestamp < cache_lines[victim].fifo_timestamp) {
                        victim = i;
                    }
                }
                return victim;
            }
            case RANDOM:
                return Utils::rand_() % num_blocks;
        }
        return 0;
    }
    
    void updateReplacementInfo(int index) {
        if (replacement_policy == LRU) {
            cache_lines[index].lru_counter = access_counter;
        } else if (replacement_policy == LFU) {
            cache_lines[index].frequency++;
        }
    }
    
    string getPolicyName() const {
        switch (replacement_policy) {
            case LRU: return "LRU";
            case LFU: return "LFU";
            case FIFO: return "FIFO";
            case RANDOM: return "Random";
            default: return "Unknown";
        }
    }
};

// ==================== Memory Trace Generators ====================

class MemoryTraceGenerator {
public:
    virtual unsigned int next() = 0;
    virtual string getName() const = 0;
    virtual void reset() = 0;
    virtual ~MemoryTraceGenerator() {}
};

class SequentialGenerator : public MemoryTraceGenerator {
private:
    unsigned int addr;
public:
    SequentialGenerator() : addr(0) {}
    unsigned int next() override { return (addr++) % DRAM_SIZE; }
    string getName() const override { return "Sequential"; }
    void reset() override { addr = 0; }
};

class RandomGenerator : public MemoryTraceGenerator {
public:
    unsigned int next() override { return Utils::rand_() % DRAM_SIZE; }
    string getName() const override { return "Random"; }
    void reset() override {}
};

class LocalizedRandomGenerator : public MemoryTraceGenerator {
private:
    int region_size;
public:
    LocalizedRandomGenerator(int region = 128*1024) : region_size(region) {}
    unsigned int next() override { return Utils::rand_() % region_size; }
    string getName() const override { return "Localized Random (" + 
                                     Utils::formatBytes(region_size) + ")"; }
    void reset() override {}
};

class StridedGenerator : public MemoryTraceGenerator {
private:
    unsigned int addr;
    int stride;
public:
    StridedGenerator(int s = 256) : addr(0), stride(s) {}
    unsigned int next() override { 
        unsigned int ret = addr;
        addr = (addr + stride) % DRAM_SIZE;
        return ret;
    }
    string getName() const override { return "Strided (stride=" + to_string(stride) + ")"; }
    void reset() override { addr = 0; }
};

class TemporalLocalityGenerator : public MemoryTraceGenerator {
private:
    vector<unsigned int> hot_set;
    int hot_probability;  // 0-100
    
public:
    TemporalLocalityGenerator(int hot_size = 100, int hot_prob = 80) 
        : hot_probability(hot_prob) {
        for (int i = 0; i < hot_size; i++) {
            hot_set.push_back(Utils::rand_() % DRAM_SIZE);
        }
    }
    
    unsigned int next() override {
        if ((Utils::rand_() % 100) < hot_probability) {
            return hot_set[Utils::rand_() % hot_set.size()];
        } else {
            return Utils::rand_() % DRAM_SIZE;
        }
    }
    
    string getName() const override { 
        return "Temporal Locality (" + to_string(hot_probability) + "% hot)"; 
    }
    void reset() override {}
};

// ==================== Benchmark Framework ====================

class Benchmark {
private:
    string name;
    shared_ptr<MemoryTraceGenerator> trace_gen;
    int num_accesses;
    int read_percentage;  // 0-100
    
public:
    Benchmark(const string& n, shared_ptr<MemoryTraceGenerator> gen, 
              int accesses = 1000000, int read_pct = 70)
        : name(n), trace_gen(gen), num_accesses(accesses), 
          read_percentage(read_pct) {}
    
    void run(Cache& cache) {
        cout << "\n" << string(60, '=') << "\n";
        cout << "Running Benchmark: " << name << "\n";
        cout << "Trace Pattern: " << trace_gen->getName() << "\n";
        cout << "Accesses: " << num_accesses << " (" << read_percentage 
             << "% reads, " << (100-read_percentage) << "% writes)\n";
        cout << string(60, '=') << "\n";
        
        cache.resetStats();
        trace_gen->reset();
        
        auto start = chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_accesses; i++) {
            unsigned int addr = trace_gen->next();
            AccessType type = (Utils::rand_() % 100 < read_percentage) ? READ : WRITE;
            cache.access(addr, type);
        }
        
        auto end = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed = end - start;
        
        CacheStats stats = cache.getStats();
        stats.simulation_time = elapsed.count();
        stats.display(cache.getTypeName(), 0, 0);
    }
    
    string getName() const { return name; }
};

// ==================== Configuration Manager ====================

struct CacheConfig {
    CacheType type;
    int size_kb;
    int block_size;
    int associativity;  // For set-associative
    ReplacementPolicy replacement;
    WritePolicy write_policy;
    WriteAllocatePolicy write_allocate;
    
    CacheConfig() : type(DIRECT_MAPPED), size_kb(32), block_size(64),
                    associativity(4), replacement(LRU), 
                    write_policy(WRITE_BACK), write_allocate(WRITE_ALLOCATE) {}
};

class ConfigManager {
public:
    static CacheConfig loadFromConsole() {
        CacheConfig config;
        
        cout << "\n=============================================================\n";
        cout << "=              Cache Simulator Configuration                 \n";
        cout << "===============================================================\n\n";
        
        // Cache Type
        cout << "Select Cache Type:\n";
        cout << "  0 - Direct Mapped\n";
        cout << "  1 - Set Associative\n";
        cout << "  2 - Fully Associative\n";
        cout << "Choice: ";
        int type_choice;
        cin >> type_choice;
        config.type = static_cast<CacheType>(type_choice);
        
        // Cache Size
        cout << "\nCache Size (KB) [1, 2, 4, 8, 16, 32, 64]: ";
        cin >> config.size_kb;
        
        // Block Size
        cout << "Block Size (bytes) [4, 8, 16, 32, 64, 128]: ";
        cin >> config.block_size;
        
        // Associativity (for set-associative)
        if (config.type == SET_ASSOCIATIVE) {
            cout << "Associativity [2, 4, 8, 16]: ";
            cin >> config.associativity;
        }
        
        // Replacement Policy (for associative caches)
        if (config.type != DIRECT_MAPPED) {
            cout << "\nReplacement Policy:\n";
            cout << "  0 - LRU (Least Recently Used)\n";
            cout << "  1 - LFU (Least Frequently Used)\n";
            cout << "  2 - FIFO (First In First Out)\n";
            cout << "  3 - Random\n";
            cout << "Choice: ";
            int policy_choice;
            cin >> policy_choice;
            config.replacement = static_cast<ReplacementPolicy>(policy_choice);
        }
        
        // Write Policy
        cout << "\nWrite Policy:\n";
        cout << "  0 - Write-Through\n";
        cout << "  1 - Write-Back\n";
        cout << "Choice: ";
        int write_choice;
        cin >> write_choice;
        config.write_policy = static_cast<WritePolicy>(write_choice);
        
        // Write Allocate
        cout << "\nWrite Allocate Policy:\n";
        cout << "  0 - Write-Allocate\n";
        cout << "  1 - No-Write-Allocate\n";
        cout << "Choice: ";
        int alloc_choice;
        cin >> alloc_choice;
        config.write_allocate = static_cast<WriteAllocatePolicy>(alloc_choice);
        
        return config;
    }
    
    static bool validate(const CacheConfig& config) {
        if (!Utils::isPowerOfTwo(config.size_kb)) {
            cerr << "Error: Cache size must be power of 2\n";
            return false;
        }
        if (!Utils::isPowerOfTwo(config.block_size)) {
            cerr << "Error: Block size must be power of 2\n";
            return false;
        }
        if (config.block_size < 4 || config.block_size > 128) {
            cerr << "Error: Block size must be between 4 and 128 bytes\n";
            return false;
        }
        if (config.size_kb < 1 || config.size_kb > 64) {
            cerr << "Error: Cache size must be between 1KB and 64KB\n";
            return false;
        }
        if (config.type == SET_ASSOCIATIVE) {
            if (!Utils::isPowerOfTwo(config.associativity)) {
                cerr << "Error: Associativity must be power of 2\n";
                return false;
            }
            int num_sets = (config.size_kb * 1024) / (config.block_size * config.associativity);
            if (num_sets < 1) {
                cerr << "Error: Invalid associativity for given cache and block size\n";
                return false;
            }
        }
        return true;
    }
};

// ==================== Cache Factory ====================

class CacheFactory {
public:
    static shared_ptr<Cache> create(const CacheConfig& config) {
        if (!ConfigManager::validate(config)) {
            throw invalid_argument("Invalid cache configuration");
        }
        
        switch (config.type) {
            case DIRECT_MAPPED:
                return make_shared<DirectMappedCache>(
                    config.size_kb, config.block_size, config.write_policy);
                
            case SET_ASSOCIATIVE:
                return make_shared<SetAssociativeCache>(
                    config.size_kb, config.block_size, config.associativity,
                    config.replacement, config.write_policy);
                
            case FULLY_ASSOCIATIVE:
                return make_shared<FullyAssociativeCache>(
                    config.size_kb, config.block_size, 
                    config.replacement, config.write_policy);
                
            default:
                throw invalid_argument("Unknown cache type");
        }
    }
};

// ==================== Comparison Framework ====================

class ComparisonFramework {
public:
    static void compareConfigurations(vector<CacheConfig>& configs, 
                                     shared_ptr<Benchmark> benchmark) {
        cout << "\n=============================================================\n";
        cout << "=           Cache Configuration Comparison                   \n";
        cout << "================================================================\n";
        
        vector<CacheStats> results;
        
        for (auto& config : configs) {
            try {
                auto cache = CacheFactory::create(config);
                benchmark->run(*cache);
                results.push_back(cache->getStats());
            } catch (const exception& e) {
                cerr << "Error: " << e.what() << endl;
            }
        }
        
        // Display comparison table
        cout << "\n" << string(100, '=') << "\n";
        cout << "COMPARISON SUMMARY\n";
        cout << string(100, '=') << "\n";
        cout << setw(25) << left << "Configuration"
             << setw(12) << "Hit Rate"
             << setw(12) << "Miss Rate"
             << setw(15) << "Compulsory"
             << setw(15) << "Capacity"
             << setw(15) << "Conflict"
             << "\n";
        cout << string(100, '-') << "\n";
        
        for (size_t i = 0; i < results.size(); i++) {
            string name = getCacheName(configs[i]);
            cout << setw(25) << left << name
                 << setw(12) << fixed << setprecision(2) << results[i].getHitRate() << "%"
                 << setw(12) << fixed << setprecision(2) << results[i].getMissRate() << "%"
                 << setw(15) << results[i].compulsory_misses
                 << setw(15) << results[i].capacity_misses
                 << setw(15) << results[i].conflict_misses
                 << "\n";
        }
        cout << string(100, '=') << "\n";
    }
    
private:
    static string getCacheName(const CacheConfig& config) {
        string name = to_string(config.size_kb) + "KB/";
        name += to_string(config.block_size) + "B/";
        
        switch (config.type) {
            case DIRECT_MAPPED:
                name += "DM";
                break;
            case SET_ASSOCIATIVE:
                name += to_string(config.associativity) + "W";
                break;
            case FULLY_ASSOCIATIVE:
                name += "FA";
                break;
        }
        return name;
    }
};

// ==================== Menu System ====================

class MenuSystem {
public:
    static void displayMainMenu() {
        cout << "\n============================================================\n";
        cout << "=                 Cache Simulator - Main Menu              =\n";
        cout << "============================================================\n";
        cout << "=  1. Single Cache Simulation                              =\n";
        cout << "=  2. Compare Multiple Configurations                      =\n";
        cout << "=  3. Run Predefined Benchmarks                            =\n";
        cout << "=  4. Quick Test (Default Settings)                        =\n";
        cout << "=  5. Export Results to CSV                                =\n";
        cout << "=  0. Exit                                                 =\n";
        cout << "============================================================\n";
        cout << "\nChoice: ";
    }
    
    static shared_ptr<Benchmark> selectBenchmark() {
        cout << "\nSelect Memory Access Pattern:\n";
        cout << "  1 - Sequential Access\n";
        cout << "  2 - Random Access\n";
        cout << "  3 - Localized Random (128KB region)\n";
        cout << "  4 - Strided Access (256-byte stride)\n";
        cout << "  5 - Temporal Locality (80% hot set)\n";
        cout << "  6 - Mixed Workload\n";
        cout << "Choice: ";
        
        int choice;
        cin >> choice;
        
        cout << "Number of accesses [default 1000000]: ";
        int num_accesses;
        cin >> num_accesses;
        if (num_accesses <= 0) num_accesses = 1000000;
        
        cout << "Read percentage (0-100) [default 70]: ";
        int read_pct;
        cin >> read_pct;
        if (read_pct < 0 || read_pct > 100) read_pct = 70;
        
        shared_ptr<MemoryTraceGenerator> gen;
        string name;
        
        switch (choice) {
            case 1:
                gen = make_shared<SequentialGenerator>();
                name = "Sequential Access Pattern";
                break;
            case 2:
                gen = make_shared<RandomGenerator>();
                name = "Random Access Pattern";
                break;
            case 3:
                gen = make_shared<LocalizedRandomGenerator>(128*1024);
                name = "Localized Random Pattern";
                break;
            case 4:
                gen = make_shared<StridedGenerator>(256);
                name = "Strided Access Pattern";
                break;
            case 5:
                gen = make_shared<TemporalLocalityGenerator>(100, 80);
                name = "Temporal Locality Pattern";
                break;
            case 6:
                gen = make_shared<SequentialGenerator>();  // Could be mixed
                name = "Mixed Workload";
                break;
            default:
                gen = make_shared<SequentialGenerator>();
                name = "Default Pattern";
        }
        
        return make_shared<Benchmark>(name, gen, num_accesses, read_pct);
    }
};

// ===================== Predefined Test Suites =====================

class TestSuite {
public:
    static void runBasicTests() {
        cout << "\n=================================================\n";
        cout << "   Running Basic Test Suite\n";
        cout << "=================================================\n";

        std::cout << "\n-- WORKING ON THIS FUNCTION --\n";
    }

    static void runComprehensiveTests() {
        cout << "\n=================================================\n";
        cout << "   Running Comprehensive Test Suite\n";
        cout << "=================================================\n";

        std::cout << "\n-- WORKING ON THIS FUNCTION --\n";
    }
};

// ==================== Main Program ====================

void runSingleSimulation() {
    try {
        CacheConfig config = ConfigManager::loadFromConsole();
        
        if (!ConfigManager::validate(config)) {
            cerr << "Invalid configuration!\n";
            return;
        }
        
        auto cache = CacheFactory::create(config);
        auto benchmark = MenuSystem::selectBenchmark();
        
        benchmark->run(*cache);
        
        cout << "\nExport results to CSV? (y/n): ";
        char export_choice;
        cin >> export_choice;
        if (export_choice == 'y' || export_choice == 'Y') {
            cache->getStats().exportToCSV("cache_results.csv");
            cout << "Results exported to cache_results.csv\n";
        }
        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }
}

void runComparison() {
    cout << "\nHow many configurations to compare? [2-10]: ";
    int num_configs;
    cin >> num_configs;
    
    if (num_configs < 2 || num_configs > 10) {
        cout << "Invalid number of configurations.\n";
        return;
    }
    
    vector<CacheConfig> configs;
    
    for (int i = 0; i < num_configs; i++) {
        cout << "\n--- Configuration " << (i+1) << " ---\n";
        configs.push_back(ConfigManager::loadFromConsole());
    }
    
    auto benchmark = MenuSystem::selectBenchmark();
    ComparisonFramework::compareConfigurations(configs, benchmark);
}

void runQuickTest() {
    cout << "\n===============================================================\n";
    cout << "              Running Quick Test (Default Settings)         \n";
    cout << "================================================================\n";
    
    CacheConfig config;
    config.type = SET_ASSOCIATIVE;
    config.size_kb = 32;
    config.block_size = 64;
    config.associativity = 4;
    config.replacement = LRU;
    config.write_policy = WRITE_BACK;
    
    auto cache = CacheFactory::create(config);
    auto gen = make_shared<SequentialGenerator>();
    auto benchmark = make_shared<Benchmark>("Quick Test", gen, 100000, 70);
    
    benchmark->run(*cache);
}

int main() {
    srand(time(NULL));
    
    cout << R"(
=========================================
        
          CACHE SIMULATOR
        
========================================= 
    )";
    
    bool running = true;
    
    while (running) {
        MenuSystem::displayMainMenu();
        
        int choice;
        cin >> choice;
        
        switch (choice) {
            case 1:
                runSingleSimulation();
                break;
                
            case 2:
                runComparison();
                break;
                
            case 3:
                cout << "\nSelect Test Suite:\n";
                cout << "  1 - Basic Tests\n";
                cout << "  2 - Comprehensive Tests\n";
                cout << "Choice: ";
                int suite_choice;
                cin >> suite_choice;
                if (suite_choice == 1)
                    TestSuite::runBasicTests();
                else
                    TestSuite::runComprehensiveTests();
                break;
                
            case 4:
                runQuickTest();
                break;
                
            case 5:
                cout << "Results are automatically exported after each simulation.\n";
                cout << "Check cache_results.csv in the current directory.\n";
                break;
                
            case 0:
                running = false;
                cout << "\nThank you for using Cache Simulator!\n";
                break;
                
            default:
                cout << "Invalid choice. Please try again.\n";
        }
        
        if (running && choice != 0) {
            cout << "\nPress Enter to continue...";
            cin.ignore();
            cin.get();
        }
    }
    
    return 0;
}