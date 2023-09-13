import os
import sys
import numpy

'''
# SIFTデータをコピー
mkdir data
cd data
cp -a /vol1/user/daisuke/data/ann/sift1m/sift .

# データのフォーマット変換
./build/apps/utils/fvecs_to_bin float data/sift/sift_learn.fvecs data/sift/sift_learn.fbin
./build/apps/utils/fvecs_to_bin float data/sift/sift_query.fvecs data/sift/sift_query.fbin

# GT作成
./build/apps/utils/compute_groundtruth  --data_type float --dist_fn l2 --base_file data/sift/sift_learn.fbin --query_file data/sift/sift_query.fbin --gt_file data/sift/sift_query_learn_gt100 --K 100

# テスト1 node size > sector size
## インデックス作成（スクラッチから作成）
rm -rf index/*
./build/apps/build_disk_index --data_type float --dist_fn l2 --data_path data/sift/sift_learn.fbin --index_path_prefix index/sift_learn -R 32 -L50 -B 0.012 -M 1 -T 16 --use_aisaq
### チェック
python python/scripts/check_aisaq_layout.py index/sift_learn 0
## インデックス作成（disk.indexから作成）
python python/apps/create_aisaq_index_from_disk_index.py index/sift_learn
### チェック
python python/scripts/check_aisaq_layout.py index/sift_learn 1


# テスト2 node size < sector size
## インデックス作成（スクラッチから作成）
rm -rf index/*
./build/apps/build_disk_index --data_type float --dist_fn l2 --data_path data/sift/sift_learn.fbin --index_path_prefix index/sift_learn -R 32 -L50 -B 0.003 -M 1 -T 16 --use_aisaq
### チェック
python python/scripts/check_aisaq_layout.py index/sift_learn 0
## インデックス作成（disk.indexから作成）
python python/apps/create_aisaq_index_from_disk_index.py index/sift_learn
### チェック
python python/scripts/check_aisaq_layout.py index/sift_learn 1
'''

prefix = sys.argv[1]

SECTOR_LEN = 4096

if len(sys.argv) == 2 or (len(sys.argv) == 3 and int(sys.argv[2]) == 0):
    target_file_name = f'{prefix}_aisaq.index'
elif (len(sys.argv) == 3 and int(sys.argv[2]) == 1):
    target_file_name = f'{prefix}_aisaq_py.index'

print(f'checking: {target_file_name}...')

with open(target_file_name, 'rb') as f_a:
    with open(f'{prefix}_disk.index', 'rb') as f_d:
        num_meta_a = numpy.frombuffer(f_a.read(4), dtype=numpy.uint32)
        num_meta_d = numpy.frombuffer(f_d.read(4), dtype=numpy.uint32)
        print(f'meta: {num_meta_a}, {num_meta_d}')
        num_1_a = numpy.frombuffer(f_a.read(4), dtype=numpy.uint32)
        num_1_d = numpy.frombuffer(f_d.read(4), dtype=numpy.uint32)
        print(f'1: {num_1_a}, {num_1_d}, {num_1_a[0] == num_1_d[0]}')
        npts_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        npts_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'npts: {npts_a}, {npts_d}, {npts_a[0] == npts_d[0]}')
        ndims_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        ndims_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'ndims: {ndims_a}, {ndims_d}, {ndims_a[0] == ndims_d[0]}')
        medoid_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        medoid_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'medoid: {medoid_a}, {medoid_d}, {medoid_a[0] == medoid_d[0]}')
        max_node_len_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        max_node_len_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'max_node_len: {max_node_len_a}, {max_node_len_d}')
        nnodes_per_sector_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        nnodes_per_sector_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'nnodes_per_sector: {nnodes_per_sector_a}, {nnodes_per_sector_d}')
        vamana_frozen_num_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        vamana_frozen_num_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'vamana_frozen_num: {vamana_frozen_num_a}, {vamana_frozen_num_d}, {vamana_frozen_num_a[0] == vamana_frozen_num_d[0]}')
        vamana_frozen_loc_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        vamana_frozen_loc_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'vamana_frozen_loc: {vamana_frozen_loc_a}, {vamana_frozen_loc_d}, {vamana_frozen_loc_a[0] == vamana_frozen_loc_d[0]}')
        append_reorder_data_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        append_reorder_data_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'append_reorder_data: {append_reorder_data_a}, {append_reorder_data_d}, {append_reorder_data_a[0] == append_reorder_data_d[0]}')
        disk_index_file_size_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        disk_index_file_size_d = numpy.frombuffer(f_d.read(8), dtype=numpy.uint64)
        print(f'disk_index_file_size: {disk_index_file_size_a}, {disk_index_file_size_d}')

        num_pq_chunks_a = numpy.frombuffer(f_a.read(8), dtype=numpy.uint64)
        print(f'num_pq_chunks: {num_pq_chunks_a}')

        disk_read_block_size_a = int((max_node_len_a[0] // SECTOR_LEN) + (max_node_len_a[0] % SECTOR_LEN != 0)) * SECTOR_LEN 
        disk_read_block_size_d = SECTOR_LEN

        f_a.seek(SECTOR_LEN - f_a.tell(), os.SEEK_CUR)  # headerは4096Bにする（必要なら後で修正。ただし結構面倒くさい）
        f_d.seek(SECTOR_LEN - f_d.tell(), os.SEEK_CUR)

        # read disk data
        width = int((max_node_len_d[0] - ndims_d[0] * 4 - 4) // 4)   # T = float32前提, widthが他のファイルのヘッダに書いてあればそこから読んでも良い

        node_vectors_d = numpy.zeros((npts_d[0], ndims_d[0]), dtype=numpy.float32)
        node_nnbrs_d = numpy.zeros((npts_d[0]), dtype=numpy.uint32)
        node_nbrs_id_d = numpy.zeros((npts_d[0], width), dtype=numpy.uint32)
        for k in range(int(disk_index_file_size_d[0] // disk_read_block_size_d) - 1):
            # print(f'# current position: {f_d.tell()}')
            for j in range(nnodes_per_sector_d[0]):
                if k * nnodes_per_sector_d[0] + j == npts_d[0]:
                    break
                node_vectors_d[int(k * nnodes_per_sector_d[0] + j)] = numpy.frombuffer(f_d.read(int(4 * ndims_d[0])), dtype=numpy.float32)
                nnbrs_d = numpy.frombuffer(f_d.read(4), dtype=numpy.uint32)
                # print(f'# current node: {k * nnodes_per_sector_d + j}, {nnbrs_d}')
                node_nnbrs_d[int(k * nnodes_per_sector_d[0] + j)] = nnbrs_d[0]
                node_nbrs_id_d[int(k * nnodes_per_sector_d[0] + j)] = numpy.frombuffer(f_d.read(int(4 * nnbrs_d[0])), dtype=numpy.uint32)

            f_d.seek((k+2) * disk_read_block_size_d - f_d.tell(), os.SEEK_CUR)

        # read aisaq data
        node_vectors_a = numpy.zeros((npts_a[0], ndims_a[0]), dtype=numpy.float32)
        node_nnbrs_a = numpy.zeros((npts_a[0]), dtype=numpy.uint32)
        node_nbrs_id_a = numpy.zeros((npts_d[0], width), dtype=numpy.uint32)
        node_nbrs_vec_a = numpy.zeros((npts_a[0], width, num_pq_chunks_a[0]), dtype=numpy.uint8)
        for k in range(int((disk_index_file_size_a[0] - SECTOR_LEN) // disk_read_block_size_a)):    # headerは4096B
            if nnodes_per_sector_a[0] == 0:
                # print(f'# current position: {f_a.tell()}')
                node_vectors_a[k] = numpy.frombuffer(f_a.read(int(4 * ndims_a[0])), dtype=numpy.float32)
                nnbrs_a = numpy.frombuffer(f_a.read(4), dtype=numpy.uint32)
                node_nnbrs_a[k] = nnbrs_a[0]
                node_nbrs_id_a[k] = numpy.frombuffer(f_a.read(int(4 * nnbrs_a[0])), dtype=numpy.uint32)
                node_nbrs_vec_a[k] = numpy.frombuffer(f_a.read(int(nnbrs_a[0] * num_pq_chunks_a[0])), dtype=numpy.uint8).reshape(width, num_pq_chunks_a[0])
            else:
                for j in range(nnodes_per_sector_a[0]):
                    if k * nnodes_per_sector_a[0] + j == npts_d[0]:
                        break
                    node_vectors_a[int(k * nnodes_per_sector_a[0] + j)] = numpy.frombuffer(f_a.read(int(4 * ndims_a[0])), dtype=numpy.float32)
                    nnbrs_a = numpy.frombuffer(f_a.read(4), dtype=numpy.uint32)
                    # print(f'# current node: {k * nnodes_per_sector_d + j}, {nnbrs_d}')
                    node_nnbrs_a[int(k * nnodes_per_sector_a[0] + j)] = nnbrs_a[0]
                    node_nbrs_id_a[int(k * nnodes_per_sector_a[0] + j)] = numpy.frombuffer(f_a.read(int(4 * nnbrs_a[0])), dtype=numpy.uint32)
                    node_nbrs_vec_a[int(k * nnodes_per_sector_a[0] + j)] = numpy.frombuffer(f_a.read(int(nnbrs_a[0] * num_pq_chunks_a[0])), dtype=numpy.uint8).reshape(width, num_pq_chunks_a[0])

            f_a.seek(SECTOR_LEN + (k+1) * disk_read_block_size_a - f_a.tell(), os.SEEK_CUR) # headerは4096B

        # print(node_vectors_d)
        # print(node_vectors_a)
        for i, (v1, v2) in enumerate(zip(node_vectors_d, node_vectors_a)):
            if not numpy.array_equal(v1, v2):
                print(i, v1, v2)
                raise ValueError

        assert numpy.array_equal(node_vectors_d, node_vectors_a)
        print(f'node vec: disk == aisaq: {numpy.array_equal(node_vectors_d, node_vectors_a)}')

        # print(node_nnbrs_d)
        # print(node_nnbrs_a)
        assert numpy.array_equal(node_nnbrs_d, node_nnbrs_a)
        print(f'node nnbrs: disk == aisaq: {numpy.array_equal(node_nnbrs_d, node_nnbrs_a)}')

        # print(node_nbrs_id_d)
        # print(node_nbrs_id_a)
        assert numpy.array_equal(node_nbrs_id_d, node_nbrs_id_a)
        print(f'nhood id: disk == aisaq: {numpy.array_equal(node_nbrs_id_d, node_nbrs_id_a)}')

# print('\nmem.index')
node_nbrs_id_ref = numpy.zeros((npts_d[0], width), dtype=numpy.uint32)
with open(f'{prefix}_mem.index', 'rb') as f:
    numpy.frombuffer(f.read(8), dtype=numpy.uint64) # "index_file_size")
    numpy.frombuffer(f.read(4), dtype=numpy.uint32) # "width_u32")
    numpy.frombuffer(f.read(4), dtype=numpy.uint32) # "medoid_u32")
    numpy.frombuffer(f.read(8), dtype=numpy.uint64) # "vamana_frozen_num")

    for j in range(npts_d[0]):
        nnbrs = numpy.frombuffer(f.read(4), dtype=numpy.uint32)
        node_nbrs_id_ref[j] = numpy.frombuffer(f.read(int(4 * nnbrs[0])), dtype=numpy.uint32)

# print(node_nbrs_id_ref)
assert numpy.array_equal(node_nbrs_id_d, node_nbrs_id_ref)
print(f'nhood id: disk == ref: {numpy.array_equal(node_nbrs_id_d, node_nbrs_id_ref)}')
assert numpy.array_equal(node_nbrs_id_a, node_nbrs_id_ref)
print(f'nhood id: aisaq == ref: {numpy.array_equal(node_nbrs_id_a, node_nbrs_id_ref)}')

# print('pq_compressed')
with open(f'{prefix}_pq_compressed.bin', 'rb') as f:
    num_points = numpy.frombuffer(f.read(4), dtype=numpy.uint32)
    # print(num_points, "num_points")
    num_pq_chunks_u32 = numpy.frombuffer(f.read(4), dtype=numpy.uint32)
    # print(num_pq_chunks_u32, "num_pq_chunks_u32")
    pq_comp_data = numpy.frombuffer(f.read(int(1 * npts_a[0] * num_pq_chunks_a[0])), dtype=numpy.uint8).reshape(npts_a[0], num_pq_chunks_a[0])


node_nbrs_vec_ref = numpy.zeros((npts_a[0], width, num_pq_chunks_a[0]), dtype=numpy.uint8)
for i in range(npts_a[0]):
    for j in range(width):
        node_nbrs_vec_ref[i, j] = pq_comp_data[node_nbrs_id_ref[i, j]]

# print(node_nbrs_vec_a)
# print(node_nbrs_vec_ref)
assert numpy.array_equal(node_nbrs_vec_a, node_nbrs_vec_ref)
print(f'nhood vec: aisaq == ref: {numpy.array_equal(node_nbrs_vec_a, node_nbrs_vec_ref)}')

print('OK!')
