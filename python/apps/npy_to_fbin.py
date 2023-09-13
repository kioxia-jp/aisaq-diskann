import numpy
import os
import sys

input_file_path = sys.argv[1]
output_file_path = os.path.splitext(input_file_path)[0] + '.fbin'

input_data = numpy.load(input_file_path)

fp = numpy.memmap(output_file_path, dtype=numpy.uint8, mode='w+', shape=((2 + input_data.shape[0] * input_data.shape[1]) * 4))    # T = float32 前提
del fp

cur = 0
fp = numpy.memmap(output_file_path, dtype=numpy.uint8, mode='r+', shape=((2 + input_data.shape[0] * input_data.shape[1]) * 4))    # T = float32 前提
fp[cur:cur+4] = numpy.frombuffer(numpy.array([input_data.shape[0]], dtype=numpy.uint32), dtype=numpy.uint8); cur += 4
fp[cur:cur+4] = numpy.frombuffer(numpy.array([input_data.shape[1]], dtype=numpy.uint32), dtype=numpy.uint8); cur += 4
del fp

fp = numpy.memmap(output_file_path, dtype=numpy.uint8, mode='r+', shape=((2 + input_data.shape[0] * input_data.shape[1]) * 4))    # T = float32 前提
for v in input_data:
    fp[cur:cur+4*input_data.shape[1]] = numpy.frombuffer(v, dtype=numpy.uint8); cur += 4*input_data.shape[1]
del fp

