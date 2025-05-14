**Usage for AiSAQ indices**
===========================

To generate AiSAQ-index, use the `apps/build_disk_index` program.
-----------------------------------------------------------------

The additional build arguments are as follows:

1. **--use_aisaq**: Enable AiSAQ index build.
2. **--inline_pq** (default is R, all inline): Set the number of pq vectors to be stored inline as part of the index node, pass `R` value to store all PQ vectors inline, pass -1 to auto select the maximal number of inline PQ vectors that will not have any impact on the index file size. value must be between -1 and `R`. Valid only with `use_aisaq` option.
3. **--rearrange**: Enable vectors rearrangement during build, when enabled, each vector will be assigned and stored with a new id, in a way that the number of IOs needed to read the PQ vectors during search will be minimal. This option is ignored if all PQ vectors are stored inline. Valid only with `use_aisaq` option.
4. **--num_entry_points** (default is none): Number of entry points that should be generated to be used as a search start points. Value must be between 1 and 1000. Valid only with `use_aisaq` option.

### Notes
- Unlike older version of AiSAQ, only one index is being built.
- A private case in which all PQ vectors are stored inline is similar to the older version of AiSAQ index.
- There is no conversion tool from non-AiSAQ index to AiSAQ index.

To search AiSAQ-index, use the `apps/search_disk_index` program.
--------------------------------------------------------------------

The additional search arguments are as follows:

1. **--use_aisaq**: Enable AiSAQ search, when enabled, the PQ vectors will be read from the media on demand.
2. **--pq_read_io_engine** (default is aio): Select IO engine to use for reading the PQ vectors from the media. Supported io-engines are `aio` and `uring`. Valid only with `use_aisaq` option.
3. **-V (--vector_beamwidth)** (default is 1): The vector beamwidth to be used for search. Value must be <= `W`. Valid only with `use_aisaq` option. 
4. **--pq_cache_size** (default is 0): PQ vectors cache DRAM size, may be specified in B, KiB, MiB, GiB or in % of the total vectors. You may use B/K/M/G/% suffix to specify this value, if no suffix, specified as the number of vectors (e.g. 0.8%, 0.6G, or 100000). Valid only with `use_aisaq` option.
5. **--pq_read_page_cache_size** (default is 0): PQ vectors read page cache DRAM size - per thread, may be specified in B, KiB, MiB or GiB, maximal value is 32MiB. You may use B/K/M/G suffix to specify this value, if no suffix, specified in Bytes (e.g. 0.012G or 3.5M). Applicable only with index that was built with `rearrange` option. Valid only with `use_aisaq` option.

### Notes
- Search in non-AiSAQ mode using AiSAQ index is supported.
- Search using older version of AiSAQ index is supported, older AiSAQ index will be automatically detected.
- Search with filter using rearranged AiSAQ index is not supported.

Examples
--------

## Build

Build an AiSAQ index with 32 bytes PQ vectors, 32 inline PQ vectors and vectors rearrangement enabled.
Note that -B 1 is ignored when --QD is specified.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--use_aisaq --inline_pq 32 --rearrange
```

Build an AiSAQ index with 32 bytes PQ vectors, auto select number of inline PQ vectors, vectors rearrangement enabled and 512 entry points.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--use_aisaq --inline_pq -1 --rearrange --num_entry_points 512
```

Build an AiSAQ index with 32 bytes PQ vectors and all PQ vectors are stored inline.

```bash
./apps/build_disk_index --data_type float --dist_fn l2 \
--data_path /path/to/dataset.bin --index_path_prefix /path/to/index \
-R 64 -L 125 -B 1 -M 128 --QD 32 \
--use_aisaq
```

## Search

Search AiSAQ index with vector-beamwidth of 2, use uring io engine to read the PQ vectors, 8 threads and 4MB of PQ read cache per thread.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
--index_path_prefix /path/to/index --query_file /path/to/query.bin \
--gt_file /path/to/groundtruth.bin --result_path /path/to/result \
-K 10 -W 4 -L 80 100 150 -T 8 \
--use_aisaq --pq_read_io_engine uring -V 2 --pq_read_page_cache_size 4M
```

Search AiSAQ index using default search parameters.
Note that index that was built with older version of AiSAQ will be automatically detected.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
 --index_path_prefix /path/to/index --query_file /path/to/query.bin \
 --gt_file /path/to/groundtruth.bin --result_path /path/to/result \
 -K 10 -W 4 -L 80 100 150 \
 --use_aisaq
```

Search AiSAQ index in non-AiSAQ mode.
Note that in non-AiSAQ search mode, the PQ vectors are loaded into DRAM.

```bash
./apps/search_disk_index --data_type float --dist_fn l2 \
 --index_path_prefix /path/to/index --query_file /path/to/query.bin \
 --gt_file /path/to/groundtruth.bin --result_path /path/to/result \
 -K 10 -W 4 -L 80 100 150
```
