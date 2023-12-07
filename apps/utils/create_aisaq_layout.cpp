// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

#include "utils.h"
#include "disk_utils.h"
#include "cached_io.h"

template <typename T> int create_disk_layout(char **argv)
{
    std::string base_file(argv[2]);
    std::string vamana_file(argv[3]);
    std::string pq_vec_file(argv[4]);
    std::string output_file(argv[5]);
    diskann::create_aisaq_layout<T>(base_file, vamana_file, pq_vec_file, output_file);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 6)
    {
        std::cout << argv[0]
                  << " data_type <float/int8/uint8> data_bin "
                     "vamana_index_file pq_vector_file output_aisaq_index_file"
                  << std::endl;
        exit(-1);
    }

    int ret_val = -1;
    if (std::string(argv[1]) == std::string("float"))
        ret_val = create_disk_layout<float>(argv);
    else if (std::string(argv[1]) == std::string("int8"))
        ret_val = create_disk_layout<int8_t>(argv);
    else if (std::string(argv[1]) == std::string("uint8"))
        ret_val = create_disk_layout<uint8_t>(argv);
    else
    {
        std::cout << "unsupported type. use int8/uint8/float " << std::endl;
        ret_val = -2;
    }
    return ret_val;
}
