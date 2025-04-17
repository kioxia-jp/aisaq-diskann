// Copyright (c) KIOXIA Corporation. All rights reserved.
// Licensed under the MIT license.
#pragma once
#include <stdint.h>

namespace diskann {

#define AIS_VERSION "0.2.0"
#define AIS_SEARCH_BATCH_PQ_DIST_LOOKUP
#define AIS_SEARCH_AGGREGATE_CACHED_PQ_VECTORS
#define AIS_SEARCH_PQ_CACHE_MAX_VECTORS_PCNT 100
#define AIS_SEARCH_PQ_CACHE_MAX_DRAM_GB             8.0f /* 8GB */
#define AIS_SEARCH_PQ_CACHE_DIRECT_THRESHOLD_PCNT   100
#define AIS_SEARCH_PQ_CACHE_DIRECT_THRESHOLD_N      (10 << 20) /* 10m */
#define AIS_SEARCH_PQ_READ_PAGE_CACHE_MAX_DRAM_MB   32.0f /* 32MB per thread */
#define AIS_REARRANGED_PQ_FILE_PAGE_SIZE_DEFAULT    diskann::defaults::SECTOR_LEN
#define AIS_INVALID_VID                             0xffffffff

enum ais_rearrange_sorter {
    __rearrange_sorter_opt_nhops = 1 << 8,
    __rearrange_sorter_opt_score = 1 << 9,
    __rearrange_sorter_opt_nnbrs = 1 << 10,

    ais_rearrange_sorter_nhops = 1 | __rearrange_sorter_opt_nhops,
    ais_rearrange_sorter_random = 2,
    ais_rearrange_sorter_nhops_score = 3 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_score,
    ais_rearrange_sorter_nhops_nnbrs = 4 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs,
    ais_rearrange_sorter_nhops_nnbrs_score = 5 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs | __rearrange_sorter_opt_score,
    ais_rearrange_sorter_nhops_score_nnbrs = 6 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs | __rearrange_sorter_opt_score,
    ais_rearrange_sorter_default = ais_rearrange_sorter_nhops,
};

enum ais_size_unit {
    ais_size_unit_vectors = 0,
    ais_size_unit_bytes,
    ais_size_unit_milli_percent,
};

enum ais_pq_cache_policy {
    ais_pq_cache_policy_bfs = 0,
    ais_pq_cache_policy_direct = 1,
    ais_pq_cache_policy_auto = 100,

    ais_pq_cache_policy_default = ais_pq_cache_policy_auto,
};

enum ais_pq_io_engine {
    ais_pq_io_engine_aio = 0,
    ais_pq_io_engine_uring,

    ais_pq_io_engine_default = ais_pq_io_engine_aio,
};

struct ais_node_placement {
    uint32_t id;
    bool is_in_cache;
    float pq_dist;
    char *ptr;
};

struct ais_search_config {
    bool aisaq_deprecated;
    /* OR */
    bool aisaq;
    uint32_t vector_beamwidth;
    enum ais_pq_io_engine pq_io_engine;
    uint64_t pq_cache_size;
    enum ais_size_unit pq_cache_size_unit;
    uint64_t pq_read_page_cache_size;
};

struct ais_rearranged_pq_compressed_vectors_file_header {
    uint32_t num_vectors;
    uint32_t vector_size;
    uint32_t page_size;
};

}