**Usage for AiSAQ indices**
===========================

To generate AiSAQ-index, use the `apps/build_disk_index` program.
-----------------------------------------------------------------

The additional build arguments are as follows:

1. **--inline_pq** (disabled by default): Set the number of pq vectors to be stored inline as part of the index node, pass 0 for auto, pass `R` value to store all PQ vectors inline, value must be <= `R`.
2. **--rearrange**: Enable vectors rearrangement during build, when enabled, each vector will be assigned and stored with a new id, in a way that the number of IOs needed to read the PQ vectors during search will be minimal.
3. **--num_entry_points**: Number of entry points that should be generated to be used as a search start points. Value must be between 1 and 256.

To search AiSAQ-index, use the `apps/search_disk_index` program.
--------------------------------------------------------------------

The additional search arguments are as follows:

1. **--aisaq_deprecated**: Enable search with an older version of AiSAQ index.
2. **--aisaq**: Enable AiSAQ search, when enabled, the PQ vectors will be read from the media on demand.
3. **--pq_read_io_engine** (default is aio): Select IO engine to use for reading the PQ vectors from the media. Supported io-engines are `aio` and `uring`. Valid only with `aisaq` option.
4. **-V (--vector_beamwidth)** (default is 1): The vector beamwidth to be used for search. Value must be <= `W`. Valid only with `aisaq` option. 
5. **--pq_cache_size** (default is 0): PQ vectors cache DRAM size, may be specified in B, KB, MB, GB or in % of the total vectors. You may use B/K/M/G/% suffix to specify this value, if no suffix, specified as the number of vectors (e.g. 0.8%, 0.6G, or 100000). Valid only with `aisaq` option.
6. **--pq_read_page_cache_size** (default is 0): PQ vectors read page cache DRAM size - per thread, may be specified in B, KB, MB or GB. You may use B/K/M/G suffix to specify this value, if no suffix, specified in Bytes. Applicable only with index that was built with `rearrange` option. Valid only with `aisaq` option.



Examples
--------

## Build

Build an AiSAQ index with 32 bytes PQ vectors, 32 inline PQ vectors and vectors rearrangement enabled.
Note that -B 1 is ignored when --QD is specified.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--inline_pq 32 --rearrange
```

Build an AiSAQ index with 32 bytes PQ vectors, auto select number of inline PQ vectors, vectors rearrangement enabled and 128 entry points.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--inline_pq 0 --rearrange --num_entry_points 128
```

Build an AiSAQ index with 32 bytes PQ vectors and all PQ vectors are stored inline.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--inline_pq 64
```

Build non-AiSAQ index with 32 bytes PQ vectors.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32
```

## Search

Search AiSAQ index with vector-beamwidth of 2, use uring io engine to read the PQ vectors, 8 threads and 4MB of PQ read cache per thread.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
--index_path_prefix /path/to/index --query_file /path/to/query.bin \
--gt_file /path/to/groundtruth.bin --result_path /path/to/result \
-K 10 -W 4 -L 80 100 150 -T 8 \
--aisaq --pq_read_io_engine uring -V 2 --pq_read_page_cache_size 4M
```

Search AiSAQ index that was built with an older version of AiSAQ.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
 --index_path_prefix /path/to/index --query_file /path/to/query.bin \
 --gt_file /path/to/groundtruth.bin --result_path /path/to/result \
 -K 10 -W 4 -L 80 100 150 \
 --aisaq_deprecated
```

Search AiSAQ index in non-AiSAQ mode.
Note that in non-AiSAQ search mode the PQ vectors are loaded into DRAM.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
 --index_path_prefix /path/to/index --query_file /path/to/query.bin \
 --gt_file /path/to/groundtruth.bin --result_path /path/to/result \
 -K 10 -W 4 -L 80 100 150
```
