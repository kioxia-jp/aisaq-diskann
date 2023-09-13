import numpy
import sys

prefix = sys.argv[1]

SECTOR_LEN = 4096

# Read header information from disk.index
with open(f'{prefix}_disk.index', 'rb') as f:
    header_disk = numpy.frombuffer(f.read(SECTOR_LEN), dtype=numpy.uint8)
    cur = 0
    num_meta = numpy.frombuffer(header_disk[cur:cur+4], dtype=numpy.uint32); cur += 4
    num_1 = numpy.frombuffer(header_disk[cur:cur+4], dtype=numpy.uint32); cur += 4
    npts = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    ndims = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    medoid = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    max_node_len = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    nnodes_per_sector = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    vamana_frozen_num = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    vamana_frozen_loc = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    append_reorder_data = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8
    disk_index_file_size = numpy.frombuffer(header_disk[cur:cur+8], dtype=numpy.uint64); cur += 8

# Read data from pq_compressed.bin
with open(f'{prefix}_pq_compressed.bin', 'rb') as f:
    npts_pq = numpy.frombuffer(f.read(4), dtype=numpy.uint32)
    num_pq_chunks_u32 = numpy.frombuffer(f.read(4), dtype=numpy.uint32)
    pq_comp_data = numpy.frombuffer(f.read(int(1 * npts_pq[0] * num_pq_chunks_u32[0])), dtype=numpy.uint8).reshape(npts_pq[0], num_pq_chunks_u32[0])

print('\n# Disk index information')
print(f'num_meta: {num_meta}')
print(f'num_1: {num_1}')
print(f'npts: {npts}')
print(f'ndims: {ndims}')
print(f'medoid: {medoid}')
print(f'max_node_len: {max_node_len}')
print(f'nnodes_per_sector: {nnodes_per_sector}')
print(f'vamana_frozen_num: {vamana_frozen_num}')
print(f'vamana_frozen_loc: {vamana_frozen_loc}')
print(f'append_reorder_data: {append_reorder_data}')
print(f'disk_index_file_size: {disk_index_file_size}')

print(f'npts_pq: {npts_pq}')
print(f'num_pq_chunks_u32: {num_pq_chunks_u32}')
# print(f'pq_comp_data.shape: {pq_comp_data.shape}')

disk_read_block_size = int((max_node_len[0] // SECTOR_LEN) + (max_node_len[0] % SECTOR_LEN != 0)) * SECTOR_LEN
print(f'disk_read_block_size: {disk_read_block_size}')


# Write aisaq index

## compute parameters
num_meta_aisaq = num_meta + 1
width = int((max_node_len[0] - ndims[0] * 4 - 4) // 4)   # T = float32前提, widthが他のファイルのヘッダに書いてあればそこから読んでも良い
max_node_len_aisaq = numpy.array([max_node_len[0] + width * num_pq_chunks_u32[0]], dtype=numpy.uint64)
nnodes_per_sector_aisaq = numpy.array([int(SECTOR_LEN // max_node_len_aisaq[0])], dtype=numpy.uint64)
disk_read_block_size_aisaq = int((max_node_len_aisaq[0] // SECTOR_LEN) + (max_node_len_aisaq[0] % SECTOR_LEN != 0)) * SECTOR_LEN
nsectors_per_node_aisaq = int(disk_read_block_size_aisaq // SECTOR_LEN)
if nnodes_per_sector_aisaq == 0:
    disk_index_file_size_aisaq = numpy.array([int((1 + nsectors_per_node_aisaq * npts[0]) * SECTOR_LEN)], dtype=numpy.uint64)
else:
    disk_index_file_size_aisaq = numpy.array([int((1 + (npts[0] // nnodes_per_sector_aisaq[0]) + ((npts[0] % nnodes_per_sector_aisaq[0]) != 0)) * SECTOR_LEN)], dtype=numpy.uint64)
print('\n# AiSAQ index information')
print(f'num_meta_aisaq: {num_meta_aisaq}')
print(f'width: {width}')
print(f'max_node_len_aisaq: {max_node_len_aisaq}')
print(f'nnodes_per_sector_aisaq: {nnodes_per_sector_aisaq}')
print(f'disk_read_block_size_aisaq: {disk_read_block_size_aisaq}')
print(f'nsectors_per_node_aisaq: {nsectors_per_node_aisaq}')
print(f'disk_index_file_size_aisaq: {disk_index_file_size_aisaq}')


## Write aisaq data
fp = numpy.memmap(f'{prefix}_aisaq_py.index', dtype=numpy.uint8, mode='w+', shape=(int(disk_index_file_size_aisaq[0] // SECTOR_LEN), SECTOR_LEN))
del fp
with open(f'{prefix}_disk.index', 'rb') as f:
    fp = numpy.memmap(f'{prefix}_aisaq_py.index', dtype=numpy.uint8, mode='w+', shape=(int(disk_index_file_size_aisaq[0] // SECTOR_LEN), SECTOR_LEN))
    if nnodes_per_sector_aisaq[0] == 0 and nnodes_per_sector[0] != 0:
        for i in range(int((npts[0] // nnodes_per_sector[0]) + ((npts[0] % nnodes_per_sector[0]) != 0))):
            f.seek(SECTOR_LEN + i * disk_read_block_size)
            for j in range(nnodes_per_sector[0]):
                block_data = numpy.frombuffer(f.read(max_node_len[0]), dtype=numpy.uint8)
                nnbrs = numpy.frombuffer(block_data[int(4*ndims[0]):int(4*ndims[0]+4)], dtype=numpy.uint32)
                # print(i * nnodes_per_sector + j, nnbrs)
                for k in range(nnbrs[0]):
                    nbr = numpy.frombuffer(block_data[int(4*ndims[0]+4+4*k):int(4*ndims[0]+4+4*k+4)], dtype=numpy.uint32)
                    block_data = numpy.concatenate([block_data, pq_comp_data[nbr[0]]])
                for k in range(nsectors_per_node_aisaq):
                    if k == nsectors_per_node_aisaq - 1:
                        fp[int(1 + i * nnodes_per_sector[0] * nsectors_per_node_aisaq + j * nsectors_per_node_aisaq + k), 0:int(max_node_len_aisaq[0] % SECTOR_LEN)] = numpy.frombuffer(block_data[k*SECTOR_LEN:], dtype=numpy.uint8)
                    else:
                        fp[int(1 + i * nnodes_per_sector[0] * nsectors_per_node_aisaq + j * nsectors_per_node_aisaq + k)] = numpy.frombuffer(block_data[k*SECTOR_LEN:(k+1)*SECTOR_LEN], dtype=numpy.uint8)
                if i * nnodes_per_sector[0] + j == npts[0] - 1:
                    break
    elif nnodes_per_sector_aisaq[0] != 0 and nnodes_per_sector[0] != 0:
        cur_target_sector = 0
        for i in range(int((npts[0] // nnodes_per_sector[0]) + ((npts[0] % nnodes_per_sector[0]) != 0))):
            f.seek(SECTOR_LEN + i * disk_read_block_size)
            for j in range(nnodes_per_sector[0]):
                block_data = numpy.frombuffer(f.read(max_node_len[0]), dtype=numpy.uint8)
                nnbrs = numpy.frombuffer(block_data[int(4*ndims[0]):int(4*ndims[0]+4)], dtype=numpy.uint32)
                # print(i * nnodes_per_sector + j, nnbrs)
                for k in range(nnbrs[0]):
                    nbr = numpy.frombuffer(block_data[int(4*ndims[0]+4+4*k):int(4*ndims[0]+4+4*k+4)], dtype=numpy.uint32)
                    block_data = numpy.concatenate([block_data, pq_comp_data[nbr[0]]])
                cur_node = i * nnodes_per_sector[0] + j
                cur_target_sector = int(1 + (cur_node // nnodes_per_sector_aisaq[0]))
                fp[cur_target_sector, int((cur_node % nnodes_per_sector_aisaq[0]) * max_node_len_aisaq[0]):int((cur_node % nnodes_per_sector_aisaq[0] + 1) * max_node_len_aisaq[0])] = numpy.frombuffer(block_data, dtype=numpy.uint8)
                if i * nnodes_per_sector[0] + j == npts[0] - 1:
                    break
    else:
        # nnodes_per_sector[0] == 0 のケースには未対応
        raise NotImplementedError
    del fp

## Write header (最後に書き込む)
fp = numpy.memmap(f'{prefix}_aisaq_py.index', dtype=numpy.uint8, mode='r+', shape=(int(npts[0] + 1), disk_read_block_size))
cur = 0
fp[0, cur:cur+4] = numpy.frombuffer(num_meta_aisaq.tobytes(), dtype=numpy.uint8); cur += 4
fp[0, cur:cur+4] = numpy.frombuffer(num_1.tobytes(), dtype=numpy.uint8); cur += 4
fp[0, cur:cur+8] = numpy.frombuffer(npts.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(ndims.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(medoid.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(max_node_len_aisaq.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(nnodes_per_sector_aisaq.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(vamana_frozen_num.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(vamana_frozen_loc.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(append_reorder_data.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+8] = numpy.frombuffer(disk_index_file_size_aisaq.tobytes(), dtype=numpy.uint8); cur += 8
fp[0, cur:cur+4] = numpy.frombuffer(num_pq_chunks_u32.tobytes(), dtype=numpy.uint8); cur += 4
del fp

# check
with open(f'{prefix}_aisaq_py.index', 'rb') as f:
    num_meta_aisaq = numpy.frombuffer(f.read(4), dtype=numpy.uint32)

