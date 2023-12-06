// Something about copyright

#include <iostream>
#include <boost/program_options.hpp>
#include "utils.h"
#include "pq_flash_index.h"
#include "linux_aligned_file_reader.h"
#include "program_options_utils.hpp"

namespace po = boost::program_options;

namespace diskann {
    template <typename T, typename LabelT>
    void PQFlashIndex<T, LabelT>::generate_aisaq_index(const std::string aisaq_index_file) {
        uint32_t nsectors_to_read_diskann = _nnodes_per_sector > 0 ? 1 : ROUND_UP(_max_node_len / defaults::SECTOR_LEN);
        uint64_t max_node_len_aisaq = this->_max_node_len + this->_max_degree * sizeof(uint8_t) * this->_n_chunks;
    }
} // end of diskann

template <typename T, typename LabelT = uint32_t>
void create_aisaq_index_from_diskann(const diskann::Metric metric, const std::string index_path_prefix, const std::string pq_file_prefix, const std::string disk_index_path, const std::string aisaq_index_path) {
    // Avoid the threads=1 bug.
    int num_threads = 2;
    std::shared_ptr<AlignedFileReader> reader = nullptr;

#ifdef _WINDOWS
#ifndef USE_BING_INFRA
    reader.reset(new WindowsAlignedFileReader());
#else
    reader.reset(new diskann::BingAlignedFileReader());
#endif
#else
    reader.reset(new LinuxAlignedFileReader());
#endif

    std::unique_ptr<diskann::PQFlashIndex<T, LabelT>> _pFlashIndex(
        new diskann::PQFlashIndex<T, LabelT>(reader, metric, false));

    if (index_path_prefix != std::string("")) {
        _pFlashIndex->load(num_threads, index_path_prefix.c_str());
    } else {
        std::string pq_compressed_path = pq_file_prefix + "_pq_compressed.bin";
        std::string pq_table_path = pq_file_prefix + "_pq_pivots.bin";

        _pFlashIndex->load_from_separate_paths(num_threads, disk_index_path.c_str(), pq_table_path.c_str(), pq_compressed_path.c_str(), "");
    }

    _pFlashIndex->generate_aisaq_index(aisaq_index_path);

}

int main(int argc, char **argv) {

    // assumes PQ vectors and pivots files have the same path prefix
    std::string data_type, dist_fn, index_path_prefix, pq_file_prefix, disk_index_bin, aisaq_index_bin;
    

    // diskann::cout << index_prefix << std::endl;


    po::options_description desc{
    program_options_utils::make_program_description("search_disk_index", "Searches on-disk DiskANN indexes")};

    desc.add_options()("help,h", "Print information on arguments");

    // Required parameters
    po::options_description required_configs("Required");
    required_configs.add_options()("data_type", po::value<std::string>(&data_type)->required(),
                                    program_options_utils::DATA_TYPE_DESCRIPTION);
    required_configs.add_options()("dist_fn", po::value<std::string>(&dist_fn)->required(),
                                    program_options_utils::DISTANCE_FUNCTION_DESCRIPTION);

    po::options_description optional_configs("Optional");
    optional_configs.add_options()("index_path_prefix", po::value<std::string>(&index_path_prefix)->default_value(std::string("")),
                                    program_options_utils::INDEX_PATH_PREFIX_DESCRIPTION);
    optional_configs.add_options()("pq_path_prefix", po::value<std::string>(&pq_file_prefix)->default_value(std::string("")));
    optional_configs.add_options()("disk_index_file", po::value<std::string>(&disk_index_bin)->default_value(std::string("")));
    optional_configs.add_options()("aisaq_index_file", po::value<std::string>(&aisaq_index_bin)->default_value(std::string("")));

    
    // Merge required and optional parameters
    desc.add(required_configs).add(optional_configs);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help"))
    {
        std::cout << desc;
        return 0;
    }
    po::notify(vm);

    if (index_path_prefix == std::string("")) {
        if (pq_file_prefix == std::string("") || disk_index_bin == std::string("") || aisaq_index_bin == std::string("")) {
            diskann::cout << "Error: Specify --index_path_prefix if you don't have other files' paths." << std::endl;
            return -1;
        }
    } else {

    }

    diskann::Metric metric;
    if (dist_fn == std::string("mips"))
    {
        metric = diskann::Metric::INNER_PRODUCT;
    }
    else if (dist_fn == std::string("l2"))
    {
        metric = diskann::Metric::L2;
    }
    else if (dist_fn == std::string("cosine"))
    {
        metric = diskann::Metric::COSINE;
    }
    else
    {
        std::cout << "Unsupported distance function. Currently only L2/ Inner "
                     "Product/Cosine are supported."
                  << std::endl;
        return -1;
    }

    if (data_type == std::string("float")) {
        create_aisaq_index_from_diskann<float>(metric, index_path_prefix, pq_file_prefix, disk_index_bin, aisaq_index_bin);
    } else if (data_type == std::string("uint8")) {
        create_aisaq_index_from_diskann<uint8_t>(metric, index_path_prefix, pq_file_prefix, disk_index_bin, aisaq_index_bin);
    } else if (data_type == std::string("int8")) {
        create_aisaq_index_from_diskann<int8_t>(metric, index_path_prefix, pq_file_prefix, disk_index_bin, aisaq_index_bin);
    } else {
        std::cerr << "Unsupported data type. Use float or int8 or uint8" << std::endl;
        return -1;
    }




    return 0;
}