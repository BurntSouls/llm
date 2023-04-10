// Internal header to be included only by llama.cpp.
// Contains wrappers around OS interfaces.

#ifndef LLAMA_UTIL_H
#define LLAMA_UTIL_H
#define _PRELOAD_MMAP_FILE 1 // when using mmap, preload the entire file to prevent loading during first token inference
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <climits>
#include <thread>

#include <string>
#include <vector>

#ifdef __has_include
    #if __has_include(<unistd.h>)
        #include <unistd.h>
        #if defined(_POSIX_MAPPED_FILES)
            #include <sys/mman.h>
        #endif
    #endif
#endif

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
    #include <stdio.h> // for _fseeki64
    typedef volatile LONG atomic_int;
    typedef atomic_int atomic_bool;

    typedef HANDLE pthread_t;
    typedef DWORD thread_ret_t;

    static int pthread_create(pthread_t *out, void *unused, thread_ret_t (*func)(void *), void *arg)
    {
        HANDLE handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
        if (handle == NULL)
        {
            return EAGAIN;
        }

        *out = handle;
        return 0;
    }

    static int pthread_join(pthread_t thread, void *unused)
    {
        return (int)WaitForSingleObject(thread, INFINITE);
    }
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <stdatomic.h>

    typedef void *thread_ret_t;
#endif

#define LLAMA_ASSERT(x) \
    do { \
        if (!(x)) { \
            fprintf(stderr, "LLAMA_ASSERT: %s:%d: %s\n", __FILE__, __LINE__, #x); \
            abort(); \
        } \
    } while (0)

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static std::string format(const char * fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int size = vsnprintf(NULL, 0, fmt, ap);
    LLAMA_ASSERT(size >= 0 && size < INT_MAX);
    std::vector<char> buf(size + 1);
    int size2 = vsnprintf(buf.data(), size + 1, fmt, ap2);
    LLAMA_ASSERT(size2 == size);
    va_end(ap2);
    va_end(ap);
    return std::string(buf.data(), size);
};

struct llama_file {
    // use FILE * so we don't have to re-open the file to mmap
    FILE * fp;
    size_t size;

    llama_file(const char * fname, const char * mode) {
        fp = std::fopen(fname, mode);
        if (fp == NULL) {
            throw format("failed to open %s: %s", fname, std::strerror(errno));
        }
        seek(0, SEEK_END);
        size = tell();
        seek(0, SEEK_SET);
    }

    size_t tell() const {
#ifdef _WIN32
        __int64 ret = _ftelli64(fp);
#else
        long ret = std::ftell(fp);
#endif
        LLAMA_ASSERT(ret != -1); // this really shouldn't fail
        return (size_t) ret;
    }

    void seek(size_t offset, int whence) {
#ifdef _WIN32
        int ret = _fseeki64(fp, (__int64) offset, whence);
#else
        int ret = std::fseek(fp, (long) offset, whence);
#endif
        LLAMA_ASSERT(ret == 0); // same
    }

    void read_raw(void * ptr, size_t size) {
        if (size == 0) {
            return;
        }
        errno = 0;
        std::size_t ret = std::fread(ptr, size, 1, fp);
        if (ferror(fp)) {
            throw format("read error: %s", strerror(errno));
        }
        if (ret != 1) {
            throw std::string("unexpectedly reached end of file");
        }
    }

    std::uint32_t read_u32() {
        std::uint32_t ret;
        read_raw(&ret, sizeof(ret));
        return ret;
    }

    std::string read_string(std::uint32_t len) {
        std::vector<char> chars(len);
        read_raw(chars.data(), len);
        return std::string(chars.data(), len);
    }

    void write_raw(const void * ptr, size_t size) {
        if (size == 0) {
            return;
        }
        errno = 0;
        size_t ret = std::fwrite(ptr, size, 1, fp);
        if (ret != 1) {
            throw format("write error: %s", strerror(errno));
        }
    }

    void write_u32(std::uint32_t val) {
        write_raw(&val, sizeof(val));
    }

    ~llama_file() {
        if (fp) {
            std::fclose(fp);
        }
    }
};

#if defined(_WIN32)
static std::string llama_format_win_err(DWORD err) {
    LPSTR buf;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);
    if (!size) {
        return "FormatMessageA failed";
    }
    std::string ret(buf, size);
    LocalFree(buf);
    return ret;
}
#endif

struct llama_mmap {
    void * addr;
    size_t size;
    typedef struct
    {
        size_t start;
        size_t end;
        void *addr;
        int n_threads;
        int n_thread;
        int page_size;
    } thread_data_t;
    static thread_ret_t worker_preload_memory(void *arg)
    {
        thread_data_t *data = (thread_data_t *)arg;
        volatile char buffer;
        for (size_t offset = data->start + data->n_thread * data->page_size; offset <= data->end; offset += data->n_threads * data->page_size)
        {
            volatile void *buffer_ptr = &buffer;
            memcpy((void *)buffer_ptr, (char *)data->addr + offset, sizeof(buffer));
            if (data->n_threads < data->n_thread && buffer==0) exit(-1); // to avoid compiler optimization - the previous simple access method did not work in thread workers
        }
        return NULL;
    }
    void preload_mmap_file(void *addr, size_t length, int n_threads)
    {
    #ifndef _PRELOAD_MMAP_FILE
        return;
    #endif
    // Get the page size of the system
    #if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        long page_size = si.dwPageSize;
    #else
        long page_size = sysconf(_SC_PAGE_SIZE); // in windows we can use GetSystemInfo:
    #endif

        if (page_size == -1)
        {
            perror("sysconf");
            return;
        }
        #ifdef _WIN32
        HANDLE hProcess = GetCurrentProcess();
        WIN32_MEMORY_RANGE_ENTRY range;
        range.VirtualAddress = addr;
        range.NumberOfBytes = length;
        // if (!VirtualLock(addr, length))    {    }; // no benefit. for systems with too little RAM we should lock a part and restrict the preload to that new length
        if (!PrefetchVirtualMemory(hProcess, 1, &range, 0)) { }; // Prefetches part of the data and signals readahead to the file system
        #else
        // todo
        //if (posix_madvise(addr, length, POSIX_MADV_WILLNEED) == -1) { }; 
        // readahead() should be the equivalent method for Linux. I don't think madvise will cause a full fetch      
        // the multi threaded read below is pseudo sequential, it also needs a test without OS level readahead in place (worst case set threads to 1 in linux or return)
        #endif

        if (n_threads > 32)
            n_threads = 32;
        pthread_t threads[32];
        thread_data_t thread_data[32];
    
        // we split the pages between the threads - that was the only reliable solution I could find
        size_t num_pages_per_thread = (length / page_size) / n_threads;
        int pages = ceil(length / page_size);
        for (int page_start = 0; page_start < pages; page_start += n_threads * num_pages_per_thread)
        {
            size_t chunk_start = page_start * page_size;
            size_t chunk_end = chunk_start + page_size * n_threads * num_pages_per_thread;
            for (int i = 0; i < n_threads; ++i)
            {
                thread_data[i].start = chunk_start;
                thread_data[i].end = chunk_end;
                if (thread_data[i].end > length)
                {
                    thread_data[i].end = length;
                }
                thread_data[i].addr = addr;
                thread_data[i].page_size = page_size;
                thread_data[i].n_threads = n_threads;
                thread_data[i].n_thread = i;
                pthread_create(&threads[i], NULL, worker_preload_memory, &thread_data[i]);
                if (thread_data[i].end == length)
                    break;
            }

            for (int i = 0; i < n_threads; ++i)
            {
                pthread_join(threads[i], NULL);
            }
            
        }
    }
    llama_mmap(const llama_mmap &) = delete;

#ifdef _POSIX_MAPPED_FILES
    static constexpr bool SUPPORTED = true;

    llama_mmap(struct llama_file * file) {
        size = file->size;
        int fd = fileno(file->fp);
        int flags = MAP_SHARED;
#ifdef __linux__
        flags |= MAP_POPULATE;
#endif
        addr = mmap(NULL, file->size, PROT_READ, flags, fd, 0);
        close(fd);
        if (addr == MAP_FAILED) {
            throw format("mmap failed: %s", strerror(errno));
        }

        // Advise the kernel to preload the mapped memory
        if (madvise(addr, file->size, MADV_WILLNEED)) {
            fprintf(stderr, "warning: madvise(.., MADV_WILLNEED) failed: %s\n",
                    strerror(errno));
        }
        // if _PRELOAD_MMAP_FILE is define, this will preload the file into the page cache efficiently
        preload_mmap_file(addr, file->size);
    }

    ~llama_mmap() {
        munmap(addr, size);
    }
#elif defined(_WIN32)
    static constexpr bool SUPPORTED = true;

    llama_mmap(struct llama_file * file) {
        size = file->size;

        HANDLE hFile = (HANDLE) _get_osfhandle(_fileno(file->fp));

        HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        DWORD error = GetLastError();
        CloseHandle(hFile);

        if (hMapping == NULL) {
            throw format("CreateFileMappingA failed: %s", llama_format_win_err(error).c_str());
        }

        addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        error = GetLastError();
        CloseHandle(hMapping);

        if (addr == NULL) {
            throw format("MapViewOfFile failed: %s", llama_format_win_err(error).c_str());
        }

        // Advise the kernel to preload the mapped memory
        WIN32_MEMORY_RANGE_ENTRY range;
        range.VirtualAddress = addr;
        range.NumberOfBytes = (SIZE_T)size;
        if (!PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0)) {
            fprintf(stderr, "warning: PrefetchVirtualMemory failed: %s\n",
                    llama_format_win_err(GetLastError()).c_str());
        }

        // if _PRELOAD_MMAP_FILE is define, this will preload the file into the page cache efficiently
        preload_mmap_file(addr, file->size, std::thread::hardware_concurrency()/2);
    }

    ~llama_mmap() {
        if (!UnmapViewOfFile(addr)) {
            fprintf(stderr, "warning: UnmapViewOfFile failed: %s\n",
                    llama_format_win_err(GetLastError()).c_str());
        }
    }
#else
    static constexpr bool SUPPORTED = false;

    llama_mmap(struct llama_file *) {
        throw std::string("mmap not supported");
    }
#endif
};

// Represents some region of memory being locked using mlock or VirtualLock;
// will automatically unlock on destruction.
struct llama_mlock {
    void * addr = NULL;
    size_t size = 0;
    bool failed_already = false;

    llama_mlock() {}
    llama_mlock(const llama_mlock &) = delete;

    ~llama_mlock() {
        if (size) {
            raw_unlock(addr, size);
        }
    }

    void init(void * addr) {
        LLAMA_ASSERT(this->addr == NULL && this->size == 0);
        this->addr = addr;
    }

    void grow_to(size_t target_size) {
        LLAMA_ASSERT(addr);
        if (failed_already) {
            return;
        }
        size_t granularity = lock_granularity();
        target_size = (target_size + granularity - 1) & ~(granularity - 1);
        if (target_size > size) {
            if (raw_lock((uint8_t *) addr + size, target_size - size)) {
                size = target_size;
            } else {
                failed_already = true;
            }
        }
    }

#ifdef _POSIX_MEMLOCK_RANGE
    static constexpr bool SUPPORTED = true;

    size_t lock_granularity() {
        return (size_t) sysconf(_SC_PAGESIZE);
    }

    #ifdef __APPLE__
        #define MLOCK_SUGGESTION \
            "Try increasing the sysctl values 'vm.user_wire_limit' and 'vm.global_user_wire_limit' and/or " \
            "decreasing 'vm.global_no_user_wire_amount'.  Also try increasing RLIMIT_MLOCK (ulimit -l).\n"
    #else
        #define MLOCK_SUGGESTION \
            "Try increasing RLIMIT_MLOCK ('ulimit -l' as root).\n"
    #endif

    bool raw_lock(const void * addr, size_t size) {
        if (!mlock(addr, size)) {
            return true;
        } else {
            fprintf(stderr, "warning: failed to mlock %zu-byte buffer (after previously locking %zu bytes): %s\n" MLOCK_SUGGESTION,
                    size, this->size, std::strerror(errno));
            return false;
        }
    }

    #undef MLOCK_SUGGESTION

    void raw_unlock(void * addr, size_t size) {
        if (munlock(addr, size)) {
            fprintf(stderr, "warning: failed to munlock buffer: %s\n", std::strerror(errno));
        }
    }
#elif defined(_WIN32)
    static constexpr bool SUPPORTED = true;

    size_t lock_granularity() {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (size_t) si.dwPageSize;
    }

    bool raw_lock(void * addr, size_t size) {
        for (int tries = 1; ; tries++) {
            if (VirtualLock(addr, size)) {
                return true;
            }
            if (tries == 2) {
                fprintf(stderr, "warning: failed to VirtualLock %zu-byte buffer (after previously locking %zu bytes): %s\n",
                        size, this->size, llama_format_win_err(GetLastError()).c_str());
                return false;
            }

            // It failed but this was only the first try; increase the working
            // set size and try again.
            SIZE_T min_ws_size, max_ws_size;
            if (!GetProcessWorkingSetSize(GetCurrentProcess(), &min_ws_size, &max_ws_size)) {
                fprintf(stderr, "warning: GetProcessWorkingSetSize failed: %s\n",
                        llama_format_win_err(GetLastError()).c_str());
                return false;
            }
            // Per MSDN: "The maximum number of pages that a process can lock
            // is equal to the number of pages in its minimum working set minus
            // a small overhead."
            // Hopefully a megabyte is enough overhead:
            size_t increment = size + 1048576;
            // The minimum must be <= the maximum, so we need to increase both:
            min_ws_size += size;
            max_ws_size += size;
            if (!SetProcessWorkingSetSize(GetCurrentProcess(), min_ws_size, max_ws_size)) {
                fprintf(stderr, "warning: SetProcessWorkingSetSize failed: %s\n",
                        llama_format_win_err(GetLastError()).c_str());
                return false;
            }
        }
    }

    void raw_unlock(void * addr, size_t size) {
        if (!VirtualUnlock(addr, size)) {
            fprintf(stderr, "warning: failed to VirtualUnlock buffer: %s\n",
                    llama_format_win_err(GetLastError()).c_str());
        }
    }
#else
    static constexpr bool SUPPORTED = false;

    void raw_lock(const void * addr, size_t size) {
        fprintf(stderr, "warning: mlock not supported on this system\n");
    }

    void raw_unlock(const void * addr, size_t size) {}
#endif
};

// Replacement for std::vector<uint8_t> that doesn't require zero-initialization.
struct llama_buffer {
    uint8_t * addr = NULL;
    size_t size = 0;

    void resize(size_t size) {
        delete[] addr;
        addr = new uint8_t[size];
        this->size = size;
    }

    ~llama_buffer() {
        delete[] addr;
    }
};
#endif
