// Copyright (c) KIOXIA Corporation. All rights reserved.
// Licensed under the MIT license.
#include "common_includes.h"

#if defined(DISKANN_RELEASE_UNUSED_TCMALLOC_MEMORY_AT_CHECKPOINTS) && defined(DISKANN_BUILD)
#include "gperftools/malloc_extension.h"
#endif

#ifdef _WINDOWS
#error "windows is not supported"
#endif

#include <tsl/robin_map.h>
#include <map>
#include <list>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <libaio.h>
#include <liburing.h>

#include "defaults.h"
#include "utils.h"
#include "aisaq_pq_reader.h"

#define SECTOR_SIZE 512

namespace diskann
{

/* reader context */
class aisaqPQReaderContext {
public:
    virtual ~aisaqPQReaderContext();
    virtual int init(const char *pq_file_path, uint32_t max_ios, uint32_t max_io_num_sectors,
                     uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size) = 0;
    virtual void cleanup() = 0;
protected:
    aisaqPQReaderContext();
    int init_common(const char *pq_file_path, int oflags, uint32_t max_ios, uint32_t max_io_num_sectors,
                    uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size);
    void cleanup_common();
    friend class aisaqPQReader;
    friend class aisaqPQReader_aio;
    friend class aisaqPQReader_uring;
    struct page_cache_node {
        page_cache_node(uint64_t page_id, uint8_t *buff)
            : m_page_id(page_id), m_buff(buff) {
        }
        uint64_t m_page_id;
        uint8_t *m_buff;
        std::list<page_cache_node>::iterator self;
    };
    int m_fd;
    uint32_t m_max_ios;
    uint32_t m_pending_io_count;
    uint32_t *m_pending_io_completion_events;
    uint32_t m_pending_io_completion_events_count;
    std::vector<uint8_t *> m_free_data_buffers;
    uint64_t m_pq_read_page_cache_size_bytes;
    tsl::robin_map<uint64_t, page_cache_node *> m_cached_data_buffers; /* page id to cache node */
    std::list<page_cache_node> m_cached_data_buffers_lru_list;
};

aisaqPQReaderContext::aisaqPQReaderContext()
	: m_fd(-1), m_pending_io_count(0)
    , m_pending_io_completion_events(nullptr), m_pending_io_completion_events_count(0)
    , m_pq_read_page_cache_size_bytes(0)
{
}

aisaqPQReaderContext::~aisaqPQReaderContext()
{
}

int aisaqPQReaderContext::init_common(const char *pq_file_path, int oflags, uint32_t max_ios
	, uint32_t max_io_num_sectors, uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size)
{
    oflags|= O_RDONLY | O_LARGEFILE;
    m_fd = open(pq_file_path, oflags);
	if (m_fd <= 0) {
        std::cerr << "failed to open PQ compressed vectors file " << pq_file_path << std::endl;
        return -1;
    }
    posix_fadvise(m_fd, 0, 0, POSIX_FADV_DONTNEED);
    m_pending_io_completion_events = new uint32_t[max_ios];
    if (m_pending_io_completion_events == nullptr) {
        cleanup_common();
        return -1;
    }
    uint32_t buff_size, nitems;
    buff_size = max_io_num_sectors * SECTOR_SIZE;
    nitems = max_ios;
    if (pq_read_page_cache_size_bytes > 0) {
        nitems+= pq_read_page_cache_size_bytes / pq_page_size;
    }
    for (unsigned int i = 0; i < nitems; i++) {
        uint8_t *buff = (uint8_t *)std::aligned_alloc(SECTOR_SIZE, buff_size);
        if (buff == nullptr) {
            cleanup_common();
            return -1;
        }
        m_free_data_buffers.push_back(buff);
    }
	m_max_ios = max_ios;
    m_pq_read_page_cache_size_bytes = pq_read_page_cache_size_bytes;
	return 0;
}

void aisaqPQReaderContext::cleanup_common()
{
    while (!m_free_data_buffers.empty()) {
        std::free(m_free_data_buffers.back());
        m_free_data_buffers.pop_back();
    }
    if (m_pending_io_completion_events != nullptr) {
        delete [] m_pending_io_completion_events;
        m_pending_io_completion_events = nullptr;
    }
    if (m_fd > 0) {
        close(m_fd);
        m_fd = -1;
    }
}

class aisaqPQReaderContext_aio : public aisaqPQReaderContext {
public:
    aisaqPQReaderContext_aio();
protected:
    virtual ~aisaqPQReaderContext_aio();
    virtual int init(const char *pq_file_path, uint32_t max_ios, uint32_t max_io_num_sectors,
                     uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size);
    virtual void cleanup();
    friend class aisaqPQReader_aio;
    struct io_data {
        struct iocb iocb;
        bool root_io;
        bool in_page_cache;
        int32_t hooked; /* index of hooked io_data index, -1 for none */
        uint64_t from_sector;
        uint64_t to_sector;
        uint32_t vector_id;
        uint32_t vector_index;
        uint32_t buff_offset;
        uint8_t *buff;
    };
private:
    io_context_t m_aio_ctx;
    struct io_data *m_io_data;
    uint32_t m_io_data_count;
    struct iocb **m_iocbs_ptr;
};

aisaqPQReaderContext_aio::aisaqPQReaderContext_aio()
	: m_aio_ctx(nullptr), m_io_data(nullptr), m_io_data_count(0), m_iocbs_ptr(nullptr)
{
}

aisaqPQReaderContext_aio::~aisaqPQReaderContext_aio()
{
}

int aisaqPQReaderContext_aio::init(const char *pq_file_path, uint32_t max_ios, uint32_t max_io_num_sectors,
                                 uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size)
{
	if (init_common(pq_file_path, O_RDONLY | O_LARGEFILE | O_NONBLOCK | O_DIRECT,
                    max_ios, max_io_num_sectors, pq_read_page_cache_size_bytes, pq_page_size) != 0) {
        return -1;
    }
    if (io_setup(max_ios, &m_aio_ctx) != 0) {
        cleanup();
        std::cerr << "failed to setup aio context" << std::endl;
        return -1;
    }
    m_io_data = new struct aisaqPQReaderContext_aio::io_data[max_ios];
    if (m_io_data == nullptr) {
		cleanup();
        return -1;
    }
    m_iocbs_ptr = new struct iocb *[max_ios];
    if (m_iocbs_ptr == nullptr) {
        cleanup();
        return -1;
    }
    return 0;
}

void aisaqPQReaderContext_aio::cleanup()
{
	if (m_iocbs_ptr != nullptr) {
		delete [] m_iocbs_ptr;
        m_iocbs_ptr = nullptr;
	}
    if (m_io_data != nullptr) {
        delete [] m_io_data;
        m_io_data = nullptr;
    }
    if (m_aio_ctx != nullptr) {
    	io_destroy(m_aio_ctx);
        m_aio_ctx = nullptr;
    }
    cleanup_common();
}

class aisaqPQReaderContext_uring : public aisaqPQReaderContext {
public:
    aisaqPQReaderContext_uring();
protected:
    virtual ~aisaqPQReaderContext_uring();
    virtual int init(const char *pq_file_path, uint32_t max_ios, uint32_t max_io_num_sectors,
                     uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size);
    virtual void cleanup();
    friend class aisaqPQReader_uring;
    struct io_data {
        bool root_io;
        bool in_page_cache;
        int32_t hooked; /* index of hooked io_data index, -1 for none */
        uint64_t from_sector;
        uint64_t to_sector;
        uint32_t vector_id;
        uint32_t vector_index;
        uint32_t buff_offset;
        uint8_t *buff;
    };
private:
    struct io_uring m_io_uring_ctx;
    bool m_io_uring_ctx_initialized;
    struct io_data *m_io_data;
    uint32_t m_io_data_count;
};

aisaqPQReaderContext_uring::aisaqPQReaderContext_uring()
	: m_io_uring_ctx_initialized(false), m_io_data(nullptr), m_io_data_count(0)
{
}

aisaqPQReaderContext_uring::~aisaqPQReaderContext_uring()
{
}

int aisaqPQReaderContext_uring::init(const char *pq_file_path, uint32_t max_ios, uint32_t max_io_num_sectors,
                                 uint64_t pq_read_page_cache_size_bytes, uint32_t pq_page_size)
{
	if (init_common(pq_file_path, O_RDONLY | O_LARGEFILE | O_DIRECT,
                    max_ios, max_io_num_sectors, pq_read_page_cache_size_bytes, pq_page_size) != 0) {
        return -1;
    }
    if (io_uring_queue_init(max_ios, &m_io_uring_ctx, 0) != 0) {
        cleanup();
        std::cerr << "failed to setup io_uring context" << std::endl;
        return -1;
    }
    m_io_uring_ctx_initialized = true;
    m_io_data = new struct aisaqPQReaderContext_uring::io_data[max_ios];
    if (m_io_data == nullptr) {
        return -1;
    }
    return 0;
}

void aisaqPQReaderContext_uring::cleanup()
{
    if (m_io_data != nullptr) {
        delete [] m_io_data;
        m_io_data = nullptr;
    }
    if (m_io_uring_ctx_initialized) {
    	io_uring_queue_exit(&m_io_uring_ctx);
        m_io_uring_ctx_initialized = false;
    }
    cleanup_common();
}

/**************************************/

/* readers */
class aisaqPQReader_aio : public aisaqPQReader {
public:
    aisaqPQReader_aio();
protected:
    virtual ~aisaqPQReader_aio();
    virtual const char *get_io_engine_name();
    virtual int init(const char *pq_file_path, bool rearranged);
    virtual void cleanup();
    virtual aisaqPQReaderContext *create_context(uint32_t max_ios, uint64_t pq_read_page_cache_size_bytes);
    virtual void destroy_context(aisaqPQReaderContext &ctx);
    virtual int read_pq_vectors_submit(aisaqPQReaderContext &ctx,
						const uint32_t *ids, const uint32_t n_ids, uint32_t &io_count);
    virtual int read_pq_vectors_wait_completion(aisaqPQReaderContext &ctx, uint32_t *read_vec,
                        uint8_t **pq_vectors, uint32_t nr_events, uint32_t max_events, uint32_t &rcount);
    virtual void read_pq_vectors_done(aisaqPQReaderContext &ctx);
private:
};

aisaqPQReader_aio::aisaqPQReader_aio()
{
}

aisaqPQReader_aio::~aisaqPQReader_aio()
{
}

const char *aisaqPQReader_aio::get_io_engine_name()
{
	return "aio";
}

int aisaqPQReader_aio::init(const char *pq_file_path, bool rearranged)
{
    int rc = init_common(pq_file_path, rearranged);
    if (rc != 0) {
        return rc;
    }
    /* add engine specific initializations */
    return 0;
}

void aisaqPQReader_aio::cleanup()
{
    uninit_common();
}

aisaqPQReaderContext *aisaqPQReader_aio::create_context(uint32_t max_ios, uint64_t pq_read_page_cache_size_bytes)
{
    if (pq_read_page_cache_size_bytes > 0 && !m_rearranged) {
    	diskann::cout << "pq read page cache may only be used with rearranged index,"
                         " disabling pq read page cache" << std::endl;
        pq_read_page_cache_size_bytes = 0;
    }
    aisaqPQReaderContext *ctx = new aisaqPQReaderContext_aio();
    if (ctx != nullptr) {
        if (ctx->init(m_pq_file_path.c_str(), max_ios, m_max_io_size_sectors,
                     pq_read_page_cache_size_bytes, m_rearranged_pq_page_size) != 0) {
            delete ctx;
        	ctx = nullptr;
        }
    }
    return ctx;
}

void aisaqPQReader_aio::destroy_context(aisaqPQReaderContext &ctx)
{
    ctx.cleanup();
    delete &ctx;
}

int aisaqPQReader_aio::read_pq_vectors_submit(aisaqPQReaderContext &ctx,
		const uint32_t *ids, const uint32_t n_ids, uint32_t &io_count)
{
    aisaqPQReaderContext_aio &aio_ctx = reinterpret_cast<aisaqPQReaderContext_aio &>(ctx);
    if (n_ids > aio_ctx.m_max_ios) {
        std::cerr << "id list size is greater than max allowed: " << ctx.m_max_ios << " < " << n_ids << std::endl;
        return -1;
    }
    std::map<uint64_t, uint32_t> sectors_map; /* start sector -> index map */
    uint32_t read_sector_count;
    struct aisaqPQReaderContext_aio::io_data *io_data;
    assert(aio_ctx.m_pending_io_count == 0);
    aio_ctx.m_pending_io_completion_events_count = 0;
    if (ctx.m_pq_read_page_cache_size_bytes > 0) {
        /* handle items in cache first, this ensures best cache utilization
           by not dropping cache items that might be needed in this iteration */
        for (uint32_t i = 0; i < n_ids; i++) {
            io_data = aio_ctx.m_io_data + i;
            calc_pq_vector_read_params(ids[i], io_data->from_sector, io_data->to_sector, io_data->buff_offset);
            read_sector_count = io_data->to_sector - io_data->from_sector + 1;
            assert(read_sector_count <= m_max_io_size_sectors);
            io_data->vector_id = ids[i];
            io_data->vector_index = i;
            uint64_t page_id = io_data->from_sector / m_rearranged_pq_sectors_per_page;
            auto iter = aio_ctx.m_cached_data_buffers.find(page_id);
            if ((io_data->in_page_cache = (iter != aio_ctx.m_cached_data_buffers.end()))) {
                /* in cache */
                struct aisaqPQReaderContext::page_cache_node &cache_node = *iter->second;
                /* move to lru back, this also ensures it will not be removed during this read sequence */
                aio_ctx.m_cached_data_buffers_lru_list.splice(aio_ctx.m_cached_data_buffers_lru_list.end(),
                                                              aio_ctx.m_cached_data_buffers_lru_list,
                                                              cache_node.self);
                io_data->hooked = -1;
                io_data->root_io = false;
                io_data->buff = cache_node.m_buff;
                /* mark as completed */
                add_pending_io_completion_event(aio_ctx, io_data->vector_index);
            }
        }
    }

    for (uint32_t i = 0; i < n_ids; i++) {
        io_data = aio_ctx.m_io_data + i;
        if (aio_ctx.m_pq_read_page_cache_size_bytes) {
            if (io_data->in_page_cache) {
                continue;
            }
        } else {
            calc_pq_vector_read_params(ids[i], io_data->from_sector, io_data->to_sector,
                                       io_data->buff_offset);
            read_sector_count = io_data->to_sector - io_data->from_sector + 1;
            assert(read_sector_count <= m_max_io_size_sectors);
            io_data->vector_id = ids[i];
            io_data->vector_index = i;
        }
        auto sectors_map_it = sectors_map.find(io_data->from_sector);
        if (sectors_map_it != sectors_map.end() &&
            aio_ctx.m_io_data[sectors_map_it->second].to_sector >= io_data->to_sector) {
            /* overlapping io, hook it */
            io_data->hooked = aio_ctx.m_io_data[sectors_map_it->second].hooked;
            aio_ctx.m_io_data[sectors_map_it->second].hooked = i;
            io_data->root_io = false;
            io_data->buff = aio_ctx.m_io_data[sectors_map_it->second].buff;
        } else {
            io_data->hooked = -1;
            if (sectors_map_it == sectors_map.end()) {
                sectors_map[io_data->from_sector] = i;
            }
            io_data->buff = get_free_data_buffer(aio_ctx);
            if (io_data->buff == nullptr) {
                std::cerr << "No available data buffers to read PQ vectors" << std::endl;
                return -1;
            }
            io_data->root_io = true;
            io_prep_pread(&io_data->iocb, aio_ctx.m_fd, io_data->buff,
                          read_sector_count * SECTOR_SIZE, io_data->from_sector * SECTOR_SIZE);
            /* must be set after io_prep_pread */
            io_data->iocb.data = io_data;
            aio_ctx.m_iocbs_ptr[aio_ctx.m_pending_io_count] = &io_data->iocb;
            aio_ctx.m_pending_io_count++;
        }
    }
    aio_ctx.m_io_data_count = n_ids;
    int ret = io_submit(aio_ctx.m_aio_ctx, (int64_t)aio_ctx.m_pending_io_count, aio_ctx.m_iocbs_ptr);
    if (ret != aio_ctx.m_pending_io_count) {
        std::cerr << "io_submit() failed; returned " << ret << ", expected=" << n_ids << ", ernno=" << errno
                  << "=" << ::strerror(-ret) << std::endl;
        return -1;
    }
    io_count = aio_ctx.m_pending_io_count;
    return 0;
}

int aisaqPQReader_aio::read_pq_vectors_wait_completion(aisaqPQReaderContext &ctx,
		uint32_t *read_vec, uint8_t **pq_vectors, uint32_t nr_events, uint32_t max_events, uint32_t &rcount)
{
    aisaqPQReaderContext_aio &aio_ctx = reinterpret_cast<aisaqPQReaderContext_aio &>(ctx);
    struct aisaqPQReaderContext_aio::io_data *io_data;
    rcount = 0;
    while (aio_ctx.m_pending_io_completion_events_count > 0 && rcount < max_events) {
        aio_ctx.m_pending_io_completion_events_count--;
        io_data = &aio_ctx.m_io_data[aio_ctx.m_pending_io_completion_events[aio_ctx.m_pending_io_completion_events_count]];
        read_vec[rcount] =  io_data->vector_index;
        pq_vectors[rcount] = io_data->buff + io_data->buff_offset;
        rcount++;
    }
    if (rcount >= nr_events) {
        return 0;
    }
    uint32_t __max_events = max_events - rcount;
    if (__max_events > 0 && aio_ctx.m_pending_io_count > 0) {
        struct io_event evts[__max_events];
        if (nr_events > __max_events) {
            nr_events = __max_events;
        }
        if (nr_events > aio_ctx.m_pending_io_count) {
            nr_events = aio_ctx.m_pending_io_count;
        }
        int ret = io_getevents(aio_ctx.m_aio_ctx, (int64_t)nr_events, (int64_t)__max_events, evts, nullptr);
        if (ret <= 0) {
            std::cerr << "io_getevents() failed; returned " << ret
                      << ", ernno=" << errno << "=" << ::strerror(-ret) << std::endl;
            return -1;
        }
        for (uint32_t i = 0; i < ret; i++) {
            io_data = (struct aisaqPQReaderContext_aio::io_data *) (evts[i].data);
            do {
                if (rcount < max_events) {
                    read_vec[rcount] = io_data->vector_index;
                    pq_vectors[rcount] = io_data->buff + io_data->buff_offset;
                    rcount++;
                } else {
                    add_pending_io_completion_event(aio_ctx, io_data->vector_index);
                }
                if (io_data->hooked == -1) {
                    break;
                }
                io_data = &aio_ctx.m_io_data[io_data->hooked];
             } while (true);
        }
        aio_ctx.m_pending_io_count-= ret;
    }
    return 0;
}

void aisaqPQReader_aio::read_pq_vectors_done(aisaqPQReaderContext &ctx)
{
    aisaqPQReaderContext_aio &aio_ctx = reinterpret_cast<aisaqPQReaderContext_aio &>(ctx);
    struct aisaqPQReaderContext_aio::io_data *io_data;
    assert(aio_ctx.m_pending_io_count == 0);
    for (uint32_t i = 0; i < aio_ctx.m_io_data_count; i++) {
        io_data = aio_ctx.m_io_data + i;
        if (io_data->root_io) {
            uint8_t *buff = io_data->buff;
            if (aio_ctx.m_pq_read_page_cache_size_bytes > 0) {
                uint64_t page_id = io_data->from_sector / m_rearranged_pq_sectors_per_page;
                //assert(aio_ctx.m_cached_data_buffers.find(page_id) == aio_ctx.m_cached_data_buffers.end());
                struct aisaqPQReaderContext::page_cache_node &cache_node =
                                aio_ctx.m_cached_data_buffers_lru_list.emplace_back(page_id, buff);
                cache_node.self = std::prev(aio_ctx.m_cached_data_buffers_lru_list.end());
                /* add to cache */
                aio_ctx.m_cached_data_buffers[page_id] = &cache_node;
            } else
            {
                aio_ctx.m_free_data_buffers.push_back(buff);
            }
        }
    }
    aio_ctx.m_io_data_count = 0;
}

/**************************************/

class aisaqPQReader_uring : public aisaqPQReader {
public:
    aisaqPQReader_uring();
protected:
    virtual ~aisaqPQReader_uring();
	virtual const char *get_io_engine_name();
    virtual int init(const char *pq_file_path, bool rearranged);
    virtual void cleanup();
    virtual aisaqPQReaderContext *create_context(uint32_t max_ios, uint64_t pq_read_page_cache_size_bytes);
    virtual void destroy_context(aisaqPQReaderContext &ctx);
    virtual int read_pq_vectors_submit(aisaqPQReaderContext &ctx, const uint32_t *ids,
                        const uint32_t n_ids, uint32_t &io_count);
    virtual int read_pq_vectors_wait_completion(aisaqPQReaderContext &ctx, uint32_t *read_vec,
                        uint8_t **pq_vectors, uint32_t nr_events, uint32_t max_events, uint32_t &rcount);
    virtual void read_pq_vectors_done(aisaqPQReaderContext &ctx);
private:
};

aisaqPQReader_uring::aisaqPQReader_uring()
{
}

aisaqPQReader_uring::~aisaqPQReader_uring()
{
}

const char *aisaqPQReader_uring::get_io_engine_name()
{
	return "uring";
}

int aisaqPQReader_uring::init(const char *pq_file_path, bool rearranged)
{
    int rc = init_common(pq_file_path, rearranged);
    if (rc != 0) {
        return rc;
    }
    /* add engine specific initializations */
    return 0;
}

void aisaqPQReader_uring::cleanup()
{
    uninit_common();
}

aisaqPQReaderContext *aisaqPQReader_uring::create_context(uint32_t max_ios, uint64_t pq_read_page_cache_size_bytes)
{
    if (pq_read_page_cache_size_bytes > 0 && !m_rearranged) {
    	diskann::cout << "pq read page cache may only be used with rearranged index,"
                         " disabling pq read page cache" << std::endl;
        pq_read_page_cache_size_bytes = 0;
    }
    aisaqPQReaderContext *ctx = new aisaqPQReaderContext_uring();
    if (ctx != nullptr) {
        if (ctx->init(m_pq_file_path.c_str(), max_ios, m_max_io_size_sectors,
                     pq_read_page_cache_size_bytes, m_rearranged_pq_page_size) != 0) {
            delete ctx;
        	ctx = nullptr;
        }
    }
    return ctx;
}

void aisaqPQReader_uring::destroy_context(aisaqPQReaderContext &ctx)
{
    ctx.cleanup();
    delete &ctx;
}

int aisaqPQReader_uring::read_pq_vectors_submit(aisaqPQReaderContext &ctx,
		const uint32_t *ids, const uint32_t n_ids, uint32_t &io_count)
{
    aisaqPQReaderContext_uring &uring_ctx = reinterpret_cast<aisaqPQReaderContext_uring &>(ctx);
    if (n_ids > uring_ctx.m_max_ios) {
        std::cerr << "id list size is greater than max allowed: " << ctx.m_max_ios << " < " << n_ids << std::endl;
        return -1;
    }
    std::map<uint64_t, uint32_t> sectors_map; /* start sector -> index map */
    uint32_t read_sector_count;
    struct aisaqPQReaderContext_uring::io_data *io_data;
    assert(uring_ctx.m_pending_io_count == 0);
    uring_ctx.m_pending_io_completion_events_count = 0;
    if (uring_ctx.m_pq_read_page_cache_size_bytes > 0) {
        /* handle items in cache first, this ensures best cache utilization
           by not dropping cache items that might be needed in this iteration */
        for (uint32_t i = 0; i < n_ids; i++) {
            io_data = uring_ctx.m_io_data + i;
            calc_pq_vector_read_params(ids[i], io_data->from_sector, io_data->to_sector, io_data->buff_offset);
            read_sector_count = io_data->to_sector - io_data->from_sector + 1;
            assert(read_sector_count <= m_max_io_size_sectors);
            io_data->vector_index = i;
            io_data->vector_id = ids[i];
            uint64_t page_id = io_data->from_sector / m_rearranged_pq_sectors_per_page;
            auto iter = uring_ctx.m_cached_data_buffers.find(page_id);
            if ((io_data->in_page_cache = (iter != uring_ctx.m_cached_data_buffers.end()))) {
                /* in cache */
                struct aisaqPQReaderContext::page_cache_node &cache_node = *iter->second;
                /* move to lru back, this also ensures it will not be removed during this read sequence */
                uring_ctx.m_cached_data_buffers_lru_list.splice(uring_ctx.m_cached_data_buffers_lru_list.end(), uring_ctx.m_cached_data_buffers_lru_list, cache_node.self);
                io_data->hooked = -1;
                io_data->root_io = false;
                io_data->buff = cache_node.m_buff;
                /* mark as completed */
                add_pending_io_completion_event(uring_ctx, io_data->vector_index);
            }
        }
    }

    for (uint32_t i = 0; i < n_ids; i++) {
        io_data = uring_ctx.m_io_data + i;
        if (uring_ctx.m_pq_read_page_cache_size_bytes) {
            if (io_data->in_page_cache) {
                continue;
            }
        } else {
            calc_pq_vector_read_params(ids[i], io_data->from_sector,
                                       io_data->to_sector, io_data->buff_offset);
            read_sector_count = io_data->to_sector - io_data->from_sector + 1;
            assert(read_sector_count <= m_max_io_size_sectors);
            io_data->vector_index = i;
            io_data->vector_id = ids[i];
        }
        auto sectors_map_it = sectors_map.find(io_data->from_sector);
        if (sectors_map_it != sectors_map.end() &&
            uring_ctx.m_io_data[sectors_map_it->second].to_sector >= io_data->to_sector) {
            /* overlapping io, hook it */
            io_data->hooked = uring_ctx.m_io_data[sectors_map_it->second].hooked;
            uring_ctx.m_io_data[sectors_map_it->second].hooked = i;
            io_data->root_io = false;
            io_data->buff = uring_ctx.m_io_data[sectors_map_it->second].buff;
        } else {
            io_data->hooked = -1;
            if (sectors_map_it == sectors_map.end()) {
                sectors_map[io_data->from_sector] = i;
            }
            io_data->buff = get_free_data_buffer(uring_ctx);
            if (io_data->buff == nullptr) {
                std::cerr << "No available data buffers to read PQ vectors" << std::endl;
                return -1;
            }
            io_data->root_io = true;
            struct io_uring_sqe *sqe = io_uring_get_sqe(&uring_ctx.m_io_uring_ctx);
            io_uring_prep_read(sqe, uring_ctx.m_fd, io_data->buff,
                          read_sector_count * SECTOR_SIZE, io_data->from_sector * SECTOR_SIZE);
            io_uring_sqe_set_data(sqe, io_data);
            uring_ctx.m_pending_io_count++;
        }
    }

    uring_ctx.m_io_data_count = n_ids;
    int ret = io_uring_submit(&uring_ctx.m_io_uring_ctx);
    if (ret != uring_ctx.m_pending_io_count) {
        std::cerr << "io_uring_submit() failed; returned " << ret << ", expected=" << n_ids << ", ernno=" << errno
                  << "=" << ::strerror(-ret) << std::endl;
        return -1;
    }
    io_count = uring_ctx.m_pending_io_count;
    return 0;

}

int aisaqPQReader_uring::read_pq_vectors_wait_completion(aisaqPQReaderContext &ctx,
		uint32_t *read_vec, uint8_t **pq_vectors, uint32_t nr_events, uint32_t max_events, uint32_t &rcount)
{
    aisaqPQReaderContext_uring &uring_ctx = reinterpret_cast<aisaqPQReaderContext_uring &>(ctx);
    struct aisaqPQReaderContext_uring::io_data *io_data;
    rcount = 0;
    while (uring_ctx.m_pending_io_completion_events_count > 0 && rcount < max_events) {
        uring_ctx.m_pending_io_completion_events_count--;
        io_data = &uring_ctx.m_io_data[uring_ctx.m_pending_io_completion_events[uring_ctx.m_pending_io_completion_events_count]];
        read_vec[rcount] =  io_data->vector_index;
        pq_vectors[rcount] = io_data->buff + io_data->buff_offset;
        rcount++;
    }
    if (rcount >= nr_events) {
        return 0;
    }
    while (rcount < nr_events && uring_ctx.m_pending_io_count > 0) {
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&uring_ctx.m_io_uring_ctx, &cqe);
        if (ret < 0) {
            std::cerr << "io_uring_wait_cqe() failed; returned " << ret
                      << ", ernno=" << errno << "=" << ::strerror(-ret) << std::endl;
            exit(-1);
        }
        io_data = (struct aisaqPQReaderContext_uring::io_data *)io_uring_cqe_get_data(cqe);
        if (cqe->res <= 0) {
            std::cerr << "io_uring read io returned with error (cqe->res=" << cqe->res << ")" << std::endl;
            exit(-1);
        }
        do {
            if (rcount < max_events) {
                read_vec[rcount] = io_data->vector_index;
                pq_vectors[rcount] = io_data->buff + io_data->buff_offset;
                rcount++;
            } else {
                add_pending_io_completion_event(uring_ctx, io_data->vector_index);
            }
            if (io_data->hooked == -1) {
                break;
            }
            io_data = &uring_ctx.m_io_data[io_data->hooked];
        } while (true);
        uring_ctx.m_pending_io_count--;
        io_uring_cqe_seen(&uring_ctx.m_io_uring_ctx, cqe);
    };
    return 0;

}

void aisaqPQReader_uring::read_pq_vectors_done(aisaqPQReaderContext &ctx)
{
    aisaqPQReaderContext_uring &uring_ctx = reinterpret_cast<aisaqPQReaderContext_uring &>(ctx);
    struct aisaqPQReaderContext_uring::io_data *io_data;
    assert(uring_ctx.m_pending_io_count == 0);
    for (uint32_t i = 0; i < uring_ctx.m_io_data_count; i++) {
        io_data = uring_ctx.m_io_data + i;
        if (io_data->root_io) {
            uint8_t *buff = io_data->buff;
            if (uring_ctx.m_pq_read_page_cache_size_bytes > 0) {
                uint64_t page_id = io_data->from_sector / m_rearranged_pq_sectors_per_page;
                //assert(uring_ctx.m_cached_data_buffers.find(page_id) == uring_ctx.m_cached_data_buffers.end());
                struct aisaqPQReaderContext::page_cache_node &cache_node =
                            uring_ctx.m_cached_data_buffers_lru_list.emplace_back(page_id, buff);
                cache_node.self = std::prev(uring_ctx.m_cached_data_buffers_lru_list.end());
                /* add to cache */
                uring_ctx.m_cached_data_buffers[page_id] = &cache_node;
            } else {
                uring_ctx.m_free_data_buffers.push_back(buff);
            }
        }
    }
    uring_ctx.m_io_data_count = 0;
}

/**************************************/

aisaqPQReader::aisaqPQReader()
{
}

aisaqPQReader::~aisaqPQReader()
{
}

int aisaqPQReader::init_common(const char *pq_file_path, bool rearranged)
{
    /* init m_block_size */
    struct stat file_stat;
    if (stat(pq_file_path, &file_stat) != 0) {
        std::cerr << "failed to stat PQ vectors file" << std::endl;
    	return -1;
    }
    char device_path[40];
    snprintf(device_path, sizeof(device_path), "/dev/block/%d:%d",
             major(file_stat.st_dev), minor(file_stat.st_dev));
    int fd = open(device_path, O_RDONLY);
    if (fd <= 0) {
        std::cerr << "failed to detect PQ vectors file block size" << std::endl;
        return -1;
    }
    ioctl(fd, BLKSSZGET, &m_block_size);
    close(fd);

    /* init m_num_vectors, m_pq_vector_size, m_rearranged_pq_page_size */
    fd = open(pq_file_path, O_RDONLY);
    if (fd <= 0) {
        std::cerr << "failed to open PQ vectors file " << pq_file_path << std::endl;
        return -1;
    }
    size_t res;
	if (rearranged) {
    	struct aisaq_rearranged_pq_compressed_vectors_file_header file_header;
    	res = read(fd, &file_header, sizeof(file_header));
        assert(res == sizeof(file_header));
    	m_num_vectors = file_header.num_vectors;
    	m_pq_vector_size = file_header.vector_size;
    	m_rearranged_pq_page_size = file_header.page_size;
	} else {
    	res = read(fd, &m_num_vectors, sizeof(uint32_t));
        assert(res == sizeof(uint32_t));
    	res = read(fd, &m_pq_vector_size, sizeof(uint32_t));
        assert(res == sizeof(uint32_t));
        m_rearranged_pq_page_size = 0;  /* invalid */
	}
	close(fd);

    /* init m_rearranged_pq_vectors_per_page, m_rearranged_pq_sectors_per_page, m_max_io_size_sectors */
    uint64_t expected_file_size;
    if (rearranged) {
    	m_rearranged_pq_vectors_per_page = m_rearranged_pq_page_size / m_pq_vector_size;
        m_rearranged_pq_sectors_per_page = m_rearranged_pq_page_size / SECTOR_SIZE;
        if (m_rearranged_pq_page_size == 0 ||
            (m_rearranged_pq_page_size % m_block_size) != 0 ||
            (m_rearranged_pq_page_size % diskann::defaults::SECTOR_LEN) != 0) {
            std::cerr << "invalid/unsupported page size " << m_rearranged_pq_page_size << std::endl;
            return -1;
        }
        m_max_io_size_sectors = m_rearranged_pq_sectors_per_page;
        expected_file_size =
                (DIV_ROUND_UP(m_num_vectors, m_rearranged_pq_vectors_per_page) * m_rearranged_pq_page_size)
            			+ diskann::defaults::SECTOR_LEN;
    } else {
        m_rearranged_pq_vectors_per_page = m_rearranged_pq_sectors_per_page = 0; /* invalid */
        m_max_io_size_sectors = ((m_pq_vector_size - 1) / SECTOR_SIZE) + 2;
        expected_file_size = (sizeof(uint32_t) * 2) + ((uint64_t) m_num_vectors * m_pq_vector_size);
    }
	/* validate file size */
    if (file_stat.st_size != expected_file_size) {
        std::cerr << "pq vectors file " << pq_file_path << " does not match meta data" << std::endl;
        return -1;
    }
    /* init m_pq_file_path, m_rearranged */
    m_pq_file_path = pq_file_path;
    m_rearranged = rearranged;
    return 0;
}

/* static public */
aisaqPQReader *aisaqPQReader::create_reader(enum aisaq_pq_io_engine io_engine,
	const char *pq_file_path, bool rearranged)
{
    aisaqPQReader *aisaq_reader;
    switch (io_engine) {
        case aisaq_pq_io_engine_aio:
            aisaq_reader = new aisaqPQReader_aio();
            break;
        case aisaq_pq_io_engine_uring:
            aisaq_reader = new aisaqPQReader_uring();
            break;
        default:
            return nullptr;
    }
    if (aisaq_reader != nullptr) {
        if (aisaq_reader->init(pq_file_path, rearranged) != 0) {
            delete aisaq_reader;
            aisaq_reader = nullptr;
        }
    }
    return aisaq_reader;
}

void aisaqPQReader::uninit_common()
{
}

/* public */
void aisaqPQReader::clear_page_cache(aisaqPQReaderContext &ctx)
{
    while (!ctx.m_cached_data_buffers_lru_list.empty()) {
        struct aisaqPQReaderContext::page_cache_node &cache_node = ctx.m_cached_data_buffers_lru_list.back();
        ctx.m_free_data_buffers.push_back(cache_node.m_buff);
        ctx.m_cached_data_buffers_lru_list.pop_back();
    }
    ctx.m_cached_data_buffers.clear();
}

/* helpers */

void aisaqPQReader::calc_pq_vector_offset_bytes(uint32_t id, uint64_t &offset_from_header, uint32_t &header_size)
{
    if (m_rearranged) {
        header_size = diskann::defaults::SECTOR_LEN;
        offset_from_header = (((uint64_t)id / m_rearranged_pq_vectors_per_page) * m_rearranged_pq_page_size) +
                             ((id % m_rearranged_pq_vectors_per_page) * m_pq_vector_size);
        return;
    }
    header_size = sizeof(uint32_t) * 2;
    offset_from_header = (uint64_t)id * m_pq_vector_size;
}

void aisaqPQReader::calc_pq_vector_read_params(uint32_t id, uint64_t &from_sector, uint64_t &to_sector, uint32_t &buff_offset)
{
    uint64_t vector_offset_from_header;
    uint32_t header_size;

    calc_pq_vector_offset_bytes(id, vector_offset_from_header, header_size);
    if (m_rearranged) {
        from_sector = (header_size / SECTOR_SIZE) +
                      (vector_offset_from_header / m_rearranged_pq_page_size) * m_rearranged_pq_sectors_per_page;
        to_sector = from_sector + m_rearranged_pq_sectors_per_page - 1;
        buff_offset = vector_offset_from_header % m_rearranged_pq_page_size;
    } else {
        uint32_t sectors_per_block = m_block_size / SECTOR_SIZE;
        from_sector = ((header_size + vector_offset_from_header) / m_block_size) * sectors_per_block;
        to_sector = ((((header_size + vector_offset_from_header + m_pq_vector_size - 1) / m_block_size) + 1) *
                     sectors_per_block) - 1;
        buff_offset = header_size + vector_offset_from_header - (from_sector * SECTOR_SIZE);
    }
}

uint8_t *aisaqPQReader::get_free_data_buffer(aisaqPQReaderContext &ctx)
{
    if (!ctx.m_free_data_buffers.empty()) {
        /* pop from free */
        uint8_t *buff = ctx.m_free_data_buffers.back();
        ctx.m_free_data_buffers.pop_back();
        return buff;
    }
    if (ctx.m_pq_read_page_cache_size_bytes > 0 && !ctx.m_cached_data_buffers_lru_list.empty()) {
        /* pop from cache */
        struct aisaqPQReaderContext::page_cache_node &cache_node = ctx.m_cached_data_buffers_lru_list.front();
        ctx.m_cached_data_buffers.erase(cache_node.m_page_id);
        ctx.m_cached_data_buffers_lru_list.pop_front();
        return cache_node.m_buff;
    }
    return nullptr;
}

void aisaqPQReader::add_pending_io_completion_event(aisaqPQReaderContext &ctx, uint32_t completed_index)
{
    ctx.m_pending_io_completion_events[ctx.m_pending_io_completion_events_count] = completed_index;
    ctx.m_pending_io_completion_events_count++;
}

}