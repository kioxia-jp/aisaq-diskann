**Usage for AiSAQ indices**
===========================

To generate AiSAQ-index, use the `apps/build_disk_index` program.
-----------------------------------------------------------------

The additional arguments are as follows:

1. **--inline_pq** (disabled by default): Set the number of pq vectors to be stored inline as part of the index node, pass 0 for auto, pass `R` value to store all PQ vectors inline, value must be <= `R`.
2. **--rearrange**: Enable vectors rearranging during build, when enabled, each vector will be assigned and stored with a new id, in a way that the number of IOs needed to read the PQ vectors during search will be minimal.
3. **--num_entry_points**: Number of entry points that should be generated to be used as a search start points. Value nust be between 1 and 256.

To search AiSAQ-index, use the `apps/search_disk_index` program.
--------------------------------------------------------------------

The additional arguments are as follows:

1. **--aisaq_deprecated**: Enable search with older version of aisaq index.
2. **--aisaq**: Enable AiSAQ search.
3. **--pq_read_io_engine** (default is aio): Select IO engine to use for reading the PQ vectors from the media. Supported io-engines are `aio` and `uring`. Valid only with `aisaq` option.
4. **-V (--vector_beamwidth)** (default is 1): The vector beamwidth to be used for search. Value must be <= `W`. Valid only with `aisaq` option. 
5. **--pq_cache_size** (default is 0): PQ vectors cache DRAM size, may be specified in B, KB, MB, GB or in % of the total vectors. You may use B/K/M/G/% suffix to specify this value, if no suffix, specified as the number of vectors (e.g. 0.8%, 0.6G, or 100000). Valid only with `aisaq` option.
6. **--pq_read_page_cache_size** (default is 0): PQ vectors read page cache DRAM size - per thread, may be specified in B, KB, MB or GB. You may use B/K/M/G suffix to specify this value, if no suffix, specified in Bytes. Applicable only with index that was built with `rearrange` option. Valid only with `aisaq` option.
