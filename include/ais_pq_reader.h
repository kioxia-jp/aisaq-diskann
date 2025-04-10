// Copyright (c) KIOXIA Corporation. All rights reserved.
// Licensed under the MIT license.
#pragma once

#include <stdint.h>
#include <string>
#include "ais.h"

namespace diskann {

class aisPQReaderContext;

class aisPQReader {
public:
    static aisPQReader *create_reader(enum ais_pq_io_engine io_engine, const char *pq_file_path, bool rearranged);
    virtual ~aisPQReader();
	virtual const char *get_io_engine_name() = 0;
    virtual int init(const char *pq_file_path, bool rearranged) = 0;
    virtual void cleanup() = 0;
    virtual aisPQReaderContext *create_context(uint32_t max_ios, uint64_t pq_read_page_cache_size_bytes) = 0;
    virtual void destroy_context(aisPQReaderContext &ctx) = 0;
    virtual int read_pq_vectors_submit(aisPQReaderContext &ctx, const uint32_t *ids, const uint32_t n_ids, uint32_t &io_count) = 0;
    virtual int read_pq_vectors_wait_completion(aisPQReaderContext &ctx, uint32_t *read_vec, uint8_t **pq_vectors, uint32_t nr_events,
                                        uint32_t max_events, uint32_t &rcount) = 0;
    virtual void read_pq_vectors_done(aisPQReaderContext &ctx) = 0;
    void clear_page_cache(aisPQReaderContext &ctx);
protected:
    aisPQReader();
    int init_common(const char *pq_file_path, bool rearranged);
    void uninit_common();
    /* helpers */
	void calc_pq_vector_offset_bytes(uint32_t id, uint64_t &offset_from_header, uint32_t &header_size);
	void calc_pq_vector_read_params(uint32_t id, uint64_t &from_sector, uint64_t &to_sector, uint32_t &buff_offset);
	uint8_t *get_free_data_buffer(aisPQReaderContext &ctx);
	void add_pending_io_completion_event(aisPQReaderContext &ctx, uint32_t completed_index);

  	std::string m_pq_file_path;
    bool m_rearranged;
    uint32_t m_rearranged_pq_page_size;   			/* applicable with rearranged only */
	uint32_t m_rearranged_pq_vectors_per_page;      /* applicable with rearranged only */
    uint32_t m_rearranged_pq_sectors_per_page;      /* applicable with rearranged only */
    uint32_t m_block_size;                          /* file block size */
    uint32_t m_num_vectors;
    uint32_t m_pq_vector_size;                      /* in bytes */
    uint32_t m_max_io_size_sectors;
};

}
