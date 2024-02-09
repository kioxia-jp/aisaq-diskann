**Usage for AiSAQ indices**
==============================

Creating an AiSAQ index from a dataset
---------------------------------------------

To generate an AiSAQ index from a raw vector dataset, use the `apps/build_disk_index` program with the `--use_aisaq` option.
If this option is enabled, the PQ vector size can be directly specified by the `--aisaq_PQ_bytes` option. The value of `-B` (`--search_DRAM_budget`) option is ignored if the `--aisaq_PQ_bytes` is specified.

The below example builds an AiSAQ index with 32 bytes PQ vectors. `-B 64` is not used to calculate the PQ vector size.

```
./apps/build_disk_index --data_type float --dist_fn l2 --data_path /path/to/dataset.bin \
                        --index_path_prefix /path/to/index -R 64 -L 125 -B 64 -M 128 -T $(nproc) \
                        --use_aisaq --aisaq_PQ_bytes 32
```

Creating an AiSAQ index from an existing DiskANN index
------------------------------------------------------

If you already have a DiskANN index, it can be converted into an AiSAQ index without rebuilding its graph by the `apps/utils/create_aisaq_layout` program. For illustrative purpose, the DiskANN index is assumed to be built with `--index_path_prefix /path/to/index`.

The `apps/utils/create_aisaq_layout` program has 5 arguments shown in below.

1. `data_type (float/uint8/int8)`: The data type of the vector dataset.
2. `data_bin`: The dataset file in .bin format.
3. `vamana_index_file`: The vamana graph index file (usually `/path/to/index_mem.index`).
4. `pq_vector_file`: The PQ compressed vector file (usually `/path/to/index_pq_compressed.bin`).
5. `output_aisaq_index_file`: The destination of the AiSAQ index file.

Here is the example of AiSAQ index creation from a DiskANN index.

```
./apps/utils/create_aisaq_layout float /path/to/dataset.bin /path/to/index_mem.index \
                                 /path/to/index_pq_compressed.bin /path/to/index_aisaq.index
```

Searching an AiSAQ index
------------------------

If you run `apps/search_disk_index` program with the `--use_aisaq` option, the program searches an AiSAQ index file (`/path/to/index_aisaq.index`) instead of the DiskANN index file (`/path/to/index_disk.index`).

Example of AiSAQ index search:

```
./apps/search_disk_index --data_type float --dist_fn l2 --index_path_prefix /path/to/index \
                         --query_file /path/to/query.bin --gt_file /path/to/groundtruth.bin \
                         --result_path /path/to/result -K 1 -W 4 -L 100 150 200 -T $(nproc) \
                         --use_aisaq
```