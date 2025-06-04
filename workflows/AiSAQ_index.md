**Usage for AiSAQ indices**
===========================

To generate an AiSAQ-index, use the `apps/build_disk_index` program.
-----------------------------------------------------------------

The additional build arguments are as follows:

1. **--use_aisaq**: Enable AiSAQ index build.
2. **--inline_pq** (default is R, all inline): Set the number of pq vectors to be stored inline as part of the index node. Pass the value `R` to store all PQ vectors inline, or pass -1 to automatically select the maximum number of inline PQ vectors that do not impact the index file size. The value must be between -1 and `R`. This option is valid only when the `use_aisaq` option is enabled.
3. **--rearrange**: Enable vectors rearrangement during the build. When this option is enabled, each vector is assigned a new ID and stored in a manner that minimizes the number of I/O operations needed to read the PQ vectors during a search. This option is ignored if all PQ vectors are stored inline, and it is valid only when the `use_aisaq` option is enabled.
4. **--num_entry_points** (default is none): Number of entry points used as start points during a search. Value must be between 1 and 1000. This option does not have any impact on existing medoid/s generation and valid only when the `use_aisaq` option is enabled.

### Notes
- When the 'use_aisaq' option is enabled, only an AiSAQ index is generated (a DiskANN index is not generated).
- When only the `use-aisaq` option is enabled, the configuration of index is identical to the one generated with older version (tag 0.1.0)
- A tool to convert from DiskANN index to AiSAQ one is not delivered.

To search with AiSAQ-index, use the `apps/search_disk_index` program.
--------------------------------------------------------------------

The additional search arguments are as follows:

1. **--use_aisaq**: Enable AiSAQ search; the PQ vectors read from the media on demand.
2. **--pq_read_io_engine** (default is aio): Select IO engine to use for reading the PQ vectors from the media. Supported io-engines are `aio` and `uring`. This option is valid only when the `use_aisaq` option is enabled.
3. **-V (--vector_beamwidth)** (default is 1): The vector beamwidth to be used for a search. Value must be <= `W`. This option is valid only when the `use_aisaq` option is enabled.
4. **--pq_cache_size** (default is 0): Specify the DRAM size for caching PQ vectors in B, KiB, MiB, GiB or as a percentage of the total vectors. You may use the B/K/M/G/% suffix to indicate this value. If no suffix is provided, the value is interpreted as the number of vectors (e.g. 0.8%, 0.6G, or 100000). This option is valid only when the `use_aisaq` option is enabled.
5. **--pq_read_page_cache_size** (default is 0): Specify the DRAM size for read page cache per thread in B, KiB, MiB or GiB. The maximum is 32MiB. You may use the B/K/M/G suffix to indicate this value. If no suffix is provided, the value is interpreted in Bytes (e.g. 0.012G or 3.5M). Applicable only to indices built with `rearrange` option. This option is valid only when the `use_aisaq` option is enabled.

### Notes
- Searching in non-AiSAQ mode using AiSAQ index is supported by not specifying `use_aisaq` option
- Searching using AiSAQ index generated with older version (tag 0.1.0) is supported. The version of the AiSAQ index is automatically detected.
- Searching with filter using a rearranged AiSAQ index is not supported.

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
Note that index generated with older version (tag 0.1.0) of AiSAQ is automatically detected.

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
