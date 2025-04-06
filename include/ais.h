#pragma once
#include <stdint.h>

#define AIS_REARRANGED_PQ_FILE_PAGE_SIZE_DEFAULT  (1 * diskann::defaults::SECTOR_LEN)
#define AIS_REARRANGED_PQ_FILE_PAGE_SIZE_MAX      (4 * diskann::defaults::SECTOR_LEN)
#define AIS_INVALID_VID 0xffffffff

enum ais_rearrange_sorter {
    __rearrange_sorter_opt_nhops = 1 << 8,
    __rearrange_sorter_opt_score = 1 << 9,
    __rearrange_sorter_opt_nnbrs = 1 << 10,

    rearrange_sorter_nhops = 1 | __rearrange_sorter_opt_nhops,
    rearrange_sorter_random = 2,
    rearrange_sorter_nhops_score = 3 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_score,
    rearrange_sorter_nhops_nnbrs = 4 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs,
    rearrange_sorter_nhops_nnbrs_score = 5 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs | __rearrange_sorter_opt_score,
    rearrange_sorter_nhops_score_nnbrs = 6 | __rearrange_sorter_opt_nhops | __rearrange_sorter_opt_nnbrs | __rearrange_sorter_opt_score,
    rearrange_sorter_default = rearrange_sorter_nhops,
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

struct ais_rearranged_pq_compressed_vectors_file_header {
    uint32_t num_vectors;
    uint32_t vector_size;
    uint32_t page_size;
};