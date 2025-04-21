// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "common_includes.h"
#include <boost/program_options.hpp>

#include <regex>
#include "index.h"
#include "disk_utils.h"
#include "math_utils.h"
#include "memory_mapper.h"
#include "partition.h"
#include "pq_flash_index.h"
#include "timer.h"
#include "percentile_stats.h"
#include "program_options_utils.hpp"
#include "ais.h"
#include "ais_utils.h"

#ifndef _WINDOWS
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "linux_aligned_file_reader.h"
#else
#ifdef USE_BING_INFRA
#include "bing_aligned_file_reader.h"
#else
#include "windows_aligned_file_reader.h"
#endif
#endif

#define WARMUP false

namespace po = boost::program_options;

void print_stats(std::string category, std::vector<float> percentiles, std::vector<float> results)
{
    diskann::cout << std::setw(20) << category << ": " << std::flush;
    for (uint32_t s = 0; s < percentiles.size(); s++)
    {
        diskann::cout << std::setw(8) << percentiles[s] << "%";
    }
    diskann::cout << std::endl;
    diskann::cout << std::setw(22) << " " << std::flush;
    for (uint32_t s = 0; s < percentiles.size(); s++)
    {
        diskann::cout << std::setw(9) << results[s];
    }
    diskann::cout << std::endl;
}

template <typename T, typename LabelT = uint32_t>
int search_disk_index(diskann::Metric &metric, const std::string &index_path_prefix,
                      const std::string &result_output_prefix, const std::string &query_file, std::string &gt_file,
                      const uint32_t num_threads, const uint32_t recall_at, const uint32_t beamwidth,
                      const uint32_t num_nodes_to_cache, const uint32_t search_io_limit,
                      const std::vector<uint32_t> &Lvec, const float fail_if_recall_below,
                      const std::vector<std::string> &query_filters, const bool use_reorder_data,
                      const struct diskann::ais_search_config &ais_search_config)
{
    diskann::cout << "Search parameters: #threads: " << num_threads;
    if (beamwidth <= 0)
        diskann::cout << ", beamwidth to be optimized for each L value" << std::flush;
    else
        diskann::cout << ", beamwidth: " << beamwidth << std::flush;

    diskann::cout << ", search-mode: ";
    if (ais_search_config.aisaq) {
        diskann::cout << "aisaq, vector-beamwidth: " << ais_search_config.vector_beamwidth
                      << ", pq-read-io-engine: " << ais_get_io_engine_string(ais_search_config.pq_io_engine)
                      << ", pq-cache-size: ";
        if (ais_search_config.pq_cache_size > 0) {
            switch (ais_search_config.pq_cache_size_unit) {
                case diskann::ais_size_unit_vectors:
                    diskann::cout << ais_search_config.pq_cache_size << " vectors";
                break;
                case diskann::ais_size_unit_bytes:
                    diskann::cout << ais_search_config.pq_cache_size << " bytes";
                break;
                case diskann::ais_size_unit_milli_percent:
                    diskann::cout << ((double)ais_search_config.pq_cache_size / 1000) << " %";
                break;
            }
        } else {
            diskann::cout << "0";
        }
        diskann::cout << ", pq-read-page-cache-size: ";
        if (ais_search_config.pq_read_page_cache_size > 0) {
            diskann::cout << ais_search_config.pq_read_page_cache_size << " bytes (per thread)";
        } else {
            diskann::cout << "0";
        }
    } else if (ais_search_config.aisaq_deprecated) {
        diskann::cout << "aisaq-deprecated";
    } else {
        diskann::cout << "disk-ann";
    }

    if (search_io_limit == std::numeric_limits<uint32_t>::max())
        diskann::cout << "." << std::endl;
    else
        diskann::cout << ", io_limit: " << search_io_limit << "." << std::endl;

    std::string warmup_query_file = index_path_prefix + "_sample_data.bin";

    // load query bin
    T *query = nullptr;
    uint32_t *gt_ids = nullptr;
    float *gt_dists = nullptr;
    size_t query_num, query_dim, query_aligned_dim, gt_num, gt_dim;
    diskann::load_aligned_bin<T>(query_file, query, query_num, query_dim, query_aligned_dim);

    bool filtered_search = false;
    if (!query_filters.empty())
    {
        filtered_search = true;
        if (query_filters.size() != 1 && query_filters.size() != query_num)
        {
            std::cout << "Error. Mismatch in number of queries and size of query "
                         "filters file"
                      << std::endl;
            return -1; // To return -1 or some other error handling?
        }
    }

    bool calc_recall_flag = false;
    if (gt_file != std::string("null") && gt_file != std::string("NULL") && file_exists(gt_file))
    {
        diskann::load_truthset(gt_file, gt_ids, gt_dists, gt_num, gt_dim);
        if (gt_num != query_num)
        {
            diskann::cout << "Error. Mismatch in number of queries and ground truth data" << std::endl;
        }
        calc_recall_flag = true;
    }

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
        new diskann::PQFlashIndex<T, LabelT>(reader, metric));

    int res = _pFlashIndex->load(num_threads, index_path_prefix.c_str(), &ais_search_config);

    if (res != 0)
    {
        return res;
    }

    if (num_nodes_to_cache > 0) {
        std::vector<uint32_t> node_list;
        diskann::cout << "Caching " << num_nodes_to_cache << " nodes around medoid(s)" << std::endl;
        _pFlashIndex->cache_bfs_levels(num_nodes_to_cache, node_list);
        // if (num_nodes_to_cache > 0)
        //     _pFlashIndex->generate_cache_list_from_sample_queries(warmup_query_file, 15, 6, num_nodes_to_cache,
        //     num_threads, node_list);
        _pFlashIndex->load_cache_list(node_list);
        node_list.clear();
        node_list.shrink_to_fit();
    }

    /* load pq cache */
    if (ais_search_config.aisaq) {
        uint64_t pq_cache_size_bytes;
        if (ais_search_config.pq_cache_size_unit == diskann::ais_size_unit_vectors) {
            pq_cache_size_bytes = ais_search_config.pq_cache_size * _pFlashIndex->get_n_chunks() * sizeof(uint8_t);
        } else if (ais_search_config.pq_cache_size_unit == diskann::ais_size_unit_milli_percent) {
            assert(ais_search_config.pq_cache_size <= 100000);
            pq_cache_size_bytes = ((ais_search_config.pq_cache_size * _pFlashIndex->get_num_points()) / 100000) *
                                                (_pFlashIndex->get_n_chunks() * sizeof(uint8_t));
        } else {
            /* bytes */
            pq_cache_size_bytes = ais_search_config.pq_cache_size;
        }
        if (pq_cache_size_bytes > 0) {
            std::string pq_compressed_vectors_path = index_path_prefix + "_pq_compressed.bin";
            _pFlashIndex->ais_load_pq_cache(pq_compressed_vectors_path, pq_cache_size_bytes, diskann::ais_pq_cache_policy_auto);
        }
    }
    if (ais_search_config.aisaq || ais_search_config.aisaq_deprecated) {
        if (_pFlashIndex->ais_init(ais_search_config, index_path_prefix.c_str()) != 0) {
            return -1;
        }
    }

    omp_set_num_threads(num_threads);

    uint64_t warmup_L = 20;
    uint64_t warmup_num = 0, warmup_dim = 0, warmup_aligned_dim = 0;
    T *warmup = nullptr;

    if (WARMUP)
    {
        if (file_exists(warmup_query_file))
        {
            diskann::load_aligned_bin<T>(warmup_query_file, warmup, warmup_num, warmup_dim, warmup_aligned_dim);
        }
        else
        {
            warmup_num = (std::min)((uint32_t)150000, (uint32_t)15000 * num_threads);
            warmup_dim = query_dim;
            warmup_aligned_dim = query_aligned_dim;
            diskann::alloc_aligned(((void **)&warmup), warmup_num * warmup_aligned_dim * sizeof(T), 8 * sizeof(T));
            std::memset(warmup, 0, warmup_num * warmup_aligned_dim * sizeof(T));
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(-128, 127);
            for (uint32_t i = 0; i < warmup_num; i++)
            {
                for (uint32_t d = 0; d < warmup_dim; d++)
                {
                    warmup[i * warmup_aligned_dim + d] = (T)dis(gen);
                }
            }
        }
        diskann::cout << "Warming up index... " << std::flush;
        std::vector<uint64_t> warmup_result_ids_64(warmup_num, 0);
        std::vector<float> warmup_result_dists(warmup_num, 0);

#pragma omp parallel for schedule(dynamic, 1)
        for (int64_t i = 0; i < (int64_t)warmup_num; i++)
        {
            _pFlashIndex->cached_beam_search(warmup + (i * warmup_aligned_dim), 1, warmup_L,
                                             warmup_result_ids_64.data() + (i * 1),
                                             warmup_result_dists.data() + (i * 1), 4);
        }
        diskann::cout << "..done" << std::endl;
    }

    diskann::cout.setf(std::ios_base::fixed, std::ios_base::floatfield);
    diskann::cout.precision(2);

    std::string recall_string = "Recall@" + std::to_string(recall_at);
    diskann::cout << std::setw(6) << "L" << std::setw(12) << "Beamwidth" << std::setw(16) << "QPS" << std::setw(16)
                  << "Mean Latency" << std::setw(16) << "99.9 Latency" << std::setw(16) << "Mean IOs" << std::setw(16)
                  << "Mean IO (us)" << std::setw(16) << "CPU (s)";
    if (calc_recall_flag)
    {
        diskann::cout << std::setw(16) << recall_string << std::endl;
    }
    else
        diskann::cout << std::endl;
    diskann::cout << "=================================================================="
                     "================================================================="
                  << std::endl;

    std::vector<std::vector<uint32_t>> query_result_ids(Lvec.size());
    std::vector<std::vector<float>> query_result_dists(Lvec.size());

    uint32_t optimized_beamwidth = 2;

    double best_recall = 0.0;

    for (uint32_t test_id = 0; test_id < Lvec.size(); test_id++)
    {
        uint32_t L = Lvec[test_id];

        if (L < recall_at)
        {
            diskann::cout << "Ignoring search with L:" << L << " since it's smaller than K:" << recall_at << std::endl;
            continue;
        }

        if (beamwidth <= 0)
        {
            diskann::cout << "Tuning beamwidth.." << std::endl;
            optimized_beamwidth =
                optimize_beamwidth(_pFlashIndex, warmup, warmup_num, warmup_aligned_dim, L, optimized_beamwidth);
        }
        else
            optimized_beamwidth = beamwidth;

        query_result_ids[test_id].resize(recall_at * query_num);
        query_result_dists[test_id].resize(recall_at * query_num);

        auto stats = new diskann::QueryStats[query_num];

        std::vector<uint64_t> query_result_ids_64(recall_at * query_num);
        auto s = std::chrono::high_resolution_clock::now();

#pragma omp parallel for schedule(dynamic, 1)
        for (int64_t i = 0; i < (int64_t)query_num; i++)
        {
            if (!filtered_search)
            {
                _pFlashIndex->cached_beam_search(query + (i * query_aligned_dim), recall_at, L,
                                                 query_result_ids_64.data() + (i * recall_at),
                                                 query_result_dists[test_id].data() + (i * recall_at),
                                                 optimized_beamwidth, use_reorder_data, stats + i,
                                                 &ais_search_config);
            }
            else
            {
                LabelT label_for_search;
                if (query_filters.size() == 1)
                { // one label for all queries
                    label_for_search = _pFlashIndex->get_converted_label(query_filters[0]);
                }
                else
                { // one label for each query
                    label_for_search = _pFlashIndex->get_converted_label(query_filters[i]);
                }
                _pFlashIndex->cached_beam_search(
                    query + (i * query_aligned_dim), recall_at, L, query_result_ids_64.data() + (i * recall_at),
                    query_result_dists[test_id].data() + (i * recall_at), optimized_beamwidth, true, label_for_search,
                    use_reorder_data, stats + i);
            }
        }
        auto e = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = e - s;
        double qps = (1.0 * query_num) / (1.0 * diff.count());

        diskann::convert_types<uint64_t, uint32_t>(query_result_ids_64.data(), query_result_ids[test_id].data(),
                                                   query_num, recall_at);

        auto mean_latency = diskann::get_mean_stats<float>(
            stats, query_num, [](const diskann::QueryStats &stats) { return stats.total_us; });

        auto latency_999 = diskann::get_percentile_stats<float>(
            stats, query_num, 0.999, [](const diskann::QueryStats &stats) { return stats.total_us; });

        auto mean_ios = diskann::get_mean_stats<uint32_t>(stats, query_num,
                                                          [](const diskann::QueryStats &stats) { return stats.n_ios; });

        auto mean_cpuus = diskann::get_mean_stats<float>(stats, query_num,
                                                         [](const diskann::QueryStats &stats) { return stats.cpu_us; });

        auto mean_io_us = diskann::get_mean_stats<float>(stats, query_num,
                                                         [](const diskann::QueryStats &stats) { return stats.io_us; });

        double recall = 0;
        if (calc_recall_flag)
        {
            recall = diskann::calculate_recall((uint32_t)query_num, gt_ids, gt_dists, (uint32_t)gt_dim,
                                               query_result_ids[test_id].data(), recall_at, recall_at);
            best_recall = std::max(recall, best_recall);
        }

        diskann::cout << std::setw(6) << L << std::setw(12) << optimized_beamwidth << std::setw(16) << qps
                      << std::setw(16) << mean_latency << std::setw(16) << latency_999 << std::setw(16) << mean_ios
                      << std::setw(16) << mean_io_us << std::setw(16) << mean_cpuus;
        if (calc_recall_flag)
        {
            diskann::cout << std::setw(16) << recall << std::endl;
        }
        else
            diskann::cout << std::endl;
        delete[] stats;
    }

    diskann::cout << "Done searching. Now saving results " << std::endl;
    uint64_t test_id = 0;
    for (auto L : Lvec)
    {
        if (L < recall_at)
            continue;

        std::string cur_result_path = result_output_prefix + "_" + std::to_string(L) + "_idx_uint32.bin";
        diskann::save_bin<uint32_t>(cur_result_path, query_result_ids[test_id].data(), query_num, recall_at);

        cur_result_path = result_output_prefix + "_" + std::to_string(L) + "_dists_float.bin";
        diskann::save_bin<float>(cur_result_path, query_result_dists[test_id++].data(), query_num, recall_at);
    }

    diskann::aligned_free(query);
    if (warmup != nullptr)
        diskann::aligned_free(warmup);
    return best_recall >= fail_if_recall_below ? 0 : -1;
}

int main(int argc, char **argv)
{
    std::string data_type, dist_fn, index_path_prefix, result_path_prefix, query_file, gt_file, filter_label,
        label_type, query_filters_file;
    uint32_t num_threads, K, W, num_nodes_to_cache, search_io_limit;
    std::vector<uint32_t> Lvec;
    bool use_reorder_data = false;
    float fail_if_recall_below = 0.0f;
    /* aisaq related params/options */
    bool aisaq = false, aisaq_deprecated = false;
    uint32_t aisaq_pq_vector_beamwidth;
    std::string aisaq_pq_read_io_engine;
    std::string aisaq_pq_cache_size_string;
    std::string aisaq_pq_read_page_cache_size_string;

    po::options_description desc{
        program_options_utils::make_program_description("search_disk_index", "Searches on-disk DiskANN indexes")};
    try
    {
        desc.add_options()("help,h", "Print information on arguments");
        desc.add_options()("version,v", "Show version information");

        // Required parameters
        po::options_description required_configs("Required");
        required_configs.add_options()("data_type", po::value<std::string>(&data_type)->required(),
                                       program_options_utils::DATA_TYPE_DESCRIPTION);
        required_configs.add_options()("dist_fn", po::value<std::string>(&dist_fn)->required(),
                                       program_options_utils::DISTANCE_FUNCTION_DESCRIPTION);
        required_configs.add_options()("index_path_prefix", po::value<std::string>(&index_path_prefix)->required(),
                                       program_options_utils::INDEX_PATH_PREFIX_DESCRIPTION);
        required_configs.add_options()("result_path", po::value<std::string>(&result_path_prefix)->required(),
                                       program_options_utils::RESULT_PATH_DESCRIPTION);
        required_configs.add_options()("query_file", po::value<std::string>(&query_file)->required(),
                                       program_options_utils::QUERY_FILE_DESCRIPTION);
        required_configs.add_options()("recall_at,K", po::value<uint32_t>(&K)->required(),
                                       program_options_utils::NUMBER_OF_RESULTS_DESCRIPTION);
        required_configs.add_options()("search_list,L",
                                       po::value<std::vector<uint32_t>>(&Lvec)->multitoken()->required(),
                                       program_options_utils::SEARCH_LIST_DESCRIPTION);

        // Optional parameters
        po::options_description optional_configs("Optional");
        optional_configs.add_options()("gt_file", po::value<std::string>(&gt_file)->default_value(std::string("null")),
                                       program_options_utils::GROUND_TRUTH_FILE_DESCRIPTION);
        optional_configs.add_options()("beamwidth,W", po::value<uint32_t>(&W)->default_value(2),
                                       program_options_utils::BEAMWIDTH);
        optional_configs.add_options()("num_nodes_to_cache", po::value<uint32_t>(&num_nodes_to_cache)->default_value(0),
                                       program_options_utils::NUMBER_OF_NODES_TO_CACHE);
        optional_configs.add_options()(
            "search_io_limit",
            po::value<uint32_t>(&search_io_limit)->default_value(std::numeric_limits<uint32_t>::max()),
            "Max #IOs for search.  Default value: uint32::max()");
        optional_configs.add_options()("num_threads,T",
                                       po::value<uint32_t>(&num_threads)->default_value(omp_get_num_procs()),
                                       program_options_utils::NUMBER_THREADS_DESCRIPTION);
        optional_configs.add_options()("use_reorder_data", po::bool_switch()->default_value(false),
                                       "Include full precision data in the index. Use only in "
                                       "conjuction with compressed data on SSD.  Default value: false");
        optional_configs.add_options()("filter_label",
                                       po::value<std::string>(&filter_label)->default_value(std::string("")),
                                       program_options_utils::FILTER_LABEL_DESCRIPTION);
        optional_configs.add_options()("query_filters_file",
                                       po::value<std::string>(&query_filters_file)->default_value(std::string("")),
                                       program_options_utils::FILTERS_FILE_DESCRIPTION);
        optional_configs.add_options()("label_type", po::value<std::string>(&label_type)->default_value("uint"),
                                       program_options_utils::LABEL_TYPE_DESCRIPTION);
        optional_configs.add_options()("fail_if_recall_below",
                                       po::value<float>(&fail_if_recall_below)->default_value(0.0f),
                                       program_options_utils::FAIL_IF_RECALL_BELOW);
        /* aisaq params/options */
        optional_configs.add_options()("aisaq_deprecated",
                                       po::bool_switch(&aisaq_deprecated)->default_value(false),
                                       "enable search with an older version of aisaq index.");
        optional_configs.add_options()("aisaq",
                                       po::bool_switch(&aisaq)->default_value(false),
                                       "enable aisaq search, when enabled, the PQ vectors will be read from the media on demand.");
        optional_configs.add_options()("vector_beamwidth,V", po::value<uint32_t>(&aisaq_pq_vector_beamwidth)->default_value(1),
                                       "the vector beamwidth to be used for search. value must be <= W. valid only with aisaq option.");
        optional_configs.add_options()("pq_read_io_engine",
                                        po::value<std::string>(&aisaq_pq_read_io_engine)->default_value("aio"),
                                        "pq vectors read io engine to use, one of {aio, uring}. "
                                        "valid only with aisaq option.");
        optional_configs.add_options()("pq_cache_size",
                                        po::value<std::string>(&aisaq_pq_cache_size_string)->default_value("0"),
                                       "PQ vectors cache DRAM size, may be specified in B, KB, MB, GB or in % of the total vectors. "
                                       "you may use B/K/M/G/% suffix to specify this value, if no suffix, specified as the number of vectors "
                                       "(e.g. 0.8%, 0.6G, or 100000). valid only with aisaq option.");
        optional_configs.add_options()("pq_read_page_cache_size",
                                       po::value<std::string>(&aisaq_pq_read_page_cache_size_string)->default_value("0"),
                                       "PQ vectors read page cache DRAM size - per thread, may be specified in B, KB, MB or GB. "
                                       "you may use B/K/M/G suffix to specify this value, if no suffix, specified in Bytes. "
                                       "applicable only with index that was built with rearrange option. valid only with aisaq option.");
        // Merge required and optional parameters
        desc.add(required_configs).add(optional_configs);

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help"))
        {
            std::cout << desc;
            return 0;
        }
        if (vm.count("version")) {
            std::cout << "diskann aisaq version " << AIS_VERSION << "-" << BUILD_NUMBER << std::endl
                      << "compiled: " __DATE__ << " " << __TIME__ << std::endl
                      << "git revision: " << GIT_REV << std::endl;
            return 0;
        }
        po::notify(vm);
        if (vm["use_reorder_data"].as<bool>())
            use_reorder_data = true;
        /* validate aisaq options */
        if (!vm["aisaq"].defaulted()) {
            /* aisaq was specified */
            if (!vm["aisaq_deprecated"].defaulted()) {
                /* aisaq_deprecated was specified */
                std::cerr << "you may choose aisaq or aisaq_deprecated option, not both." << std::endl;
                return -1;
            }
        } else {
            /* aisaq was not specified */
            if (!vm["vector_beamwidth"].defaulted()) {
                std::cerr << "vector_beamwidth can only be used with aisaq option." << std::endl;
                return -1;
            }
            if (!vm["pq_read_io_engine"].defaulted()) {
                std::cerr << "pq_read_io_engine can only be used with aisaq option." << std::endl;
                return -1;
            }
            if (!vm["pq_cache_size"].defaulted()) {
                std::cerr << "pq_cache_size can only be used with aisaq option." << std::endl;
                return -1;
            }
            if (!vm["pq_read_page_cache_size"].defaulted()) {
                std::cerr << "pq_read_page_cache_size can only be used with aisaq option." << std::endl;
                return -1;
            }
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << ex.what() << '\n';
        return -1;
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
        std::cerr << "Unsupported distance function. Currently only L2/ Inner "
                     "Product/Cosine are supported."
                  << std::endl;
        return -1;
    }

    if ((data_type != std::string("float")) && (metric == diskann::Metric::INNER_PRODUCT))
    {
        std::cerr << "Currently support only floating point data for Inner Product." << std::endl;
        return -1;
    }

    if (use_reorder_data && data_type != std::string("float"))
    {
        std::cerr << "Error: Reorder data for reordering currently only "
                     "supported for float data type."
                  << std::endl;
        return -1;
    }

    if (filter_label != "" && query_filters_file != "")
    {
        std::cerr << "Only one of filter_label and query_filters_file should be provided" << std::endl;
        return -1;
    }

    std::vector<std::string> query_filters;
    if (filter_label != "")
    {
        query_filters.push_back(filter_label);
    }
    else if (query_filters_file != "")
    {
        query_filters = read_file_to_vector_of_strings(query_filters_file);
    }

    struct diskann::ais_search_config aisaq_search_config;
    aisaq_search_config.aisaq_deprecated = aisaq_deprecated;
    aisaq_search_config.aisaq = aisaq;
    /* initialize with default values */
    aisaq_search_config.vector_beamwidth = 1;
    aisaq_search_config.pq_io_engine = diskann::ais_pq_io_engine_aio;
    aisaq_search_config.pq_cache_size = 0;
    aisaq_search_config.pq_read_page_cache_size = 0;
    if (aisaq) {
        /* parse and validate aisaq params/options */
        /* vector_beamwidth */
        if (aisaq_pq_vector_beamwidth > 1 && W == 0) {
            std::cerr << "invalid vector_beamwidth value." << std::endl;
            return -1;
        }
        if (aisaq_pq_vector_beamwidth < 1 || aisaq_pq_vector_beamwidth > W) {
            std::cerr << "invalid vector_beamwidth value, must be between 1 and W (" << W << ")." << std::endl;
            return -1;
        }
        aisaq_search_config.vector_beamwidth = aisaq_pq_vector_beamwidth;
        /* pq_io_engine */
        if (aisaq_pq_read_io_engine == std::string("aio")) {
            aisaq_search_config.pq_io_engine = diskann::ais_pq_io_engine_aio;
        } else if (aisaq_pq_read_io_engine == std::string("uring")) {
            aisaq_search_config.pq_io_engine = diskann::ais_pq_io_engine_uring;
        } else {
            std::cerr << "invalid pq_read_io_engine." << std::endl;
            return -1;
        }
        /* pq_cache_size & pq_cache_size_unit */
        aisaq_search_config.pq_cache_size_unit = diskann::ais_size_unit_vectors;
        while (aisaq_pq_cache_size_string != std::string("")) {
            /* handle % */
            if (aisaq_pq_cache_size_string.back() == '%') {
                aisaq_pq_cache_size_string.pop_back();
                if (!std::regex_match(aisaq_pq_cache_size_string, std::regex(R"(^\d+(\.\d+)?$)"))) {
                    std::cerr << "invalid pq_cache_size value." << std::endl;
                    return -1;
                }
                float pcnt = atof(aisaq_pq_cache_size_string.c_str());
                if (pcnt < 0 || pcnt > 100) {
                    std::cerr << "invalid pq_cache_size percent value" << std::endl;
                    return -1;
                }
                aisaq_search_config.pq_cache_size = (uint64_t)(pcnt * 1000);
                aisaq_search_config.pq_cache_size_unit = diskann::ais_size_unit_milli_percent;
                break;
            }
            /* handle B, just remove it, this will allow handling KB/MB/GB as well */
            if (aisaq_pq_cache_size_string.back() == 'B' || aisaq_pq_cache_size_string.back() == 'b') {
                aisaq_search_config.pq_cache_size_unit = diskann::ais_size_unit_bytes;
                aisaq_pq_cache_size_string.pop_back();
            }
            /* handle G/M/K */
            uint32_t size_shift = 0;
            if (aisaq_pq_cache_size_string.back() == 'G' || aisaq_pq_cache_size_string.back() == 'g') {
                size_shift = 30;
            } else if (aisaq_pq_cache_size_string.back() == 'M' || aisaq_pq_cache_size_string.back() == 'm') {
                size_shift = 20;
            } else if (aisaq_pq_cache_size_string.back() == 'K' || aisaq_pq_cache_size_string.back() == 'k') {
                size_shift = 10;
            }
            if (size_shift > 0) {
                aisaq_search_config.pq_cache_size_unit = diskann::ais_size_unit_bytes;
                aisaq_pq_cache_size_string.pop_back();
                /* G/M/K may be set with decimal point */
                if (!std::regex_match(aisaq_pq_cache_size_string, std::regex(R"(^\d+(\.\d+)?$)"))) {
                    std::cerr << "invalid pq_cache_size value." << std::endl;
                    return -1;
                }
                aisaq_search_config.pq_cache_size = (uint64_t)(atof(aisaq_pq_cache_size_string.c_str()) * (1 << size_shift));
                break;
            }
            /* Bytes/vectors, decimal point is not allowed here */
            if (!std::regex_match(aisaq_pq_cache_size_string, std::regex(R"(^\d+$)"))) {
                std::cerr << "invalid pq_cache_size value." << std::endl;
                return -1;
            }
            aisaq_search_config.pq_cache_size = (uint64_t)(atoll(aisaq_pq_cache_size_string.c_str()));
            break;
        };
        /* pq_read_page_cache_size */
        while (aisaq_pq_read_page_cache_size_string != std::string("")) {
            /* handle B, just remove it, this will allow handling KB/MB/GB as well */
            if (aisaq_pq_read_page_cache_size_string.back() == 'B' || aisaq_pq_read_page_cache_size_string.back() == 'b') {
                aisaq_pq_read_page_cache_size_string.pop_back();
            }
            /* handle G/M/K */
            uint32_t size_shift = 0;
            if (aisaq_pq_read_page_cache_size_string.back() == 'G' || aisaq_pq_read_page_cache_size_string.back() == 'g') {
                size_shift = 30;
            } else if (aisaq_pq_read_page_cache_size_string.back() == 'M' || aisaq_pq_read_page_cache_size_string.back() == 'm') {
                size_shift = 20;
            } else if (aisaq_pq_read_page_cache_size_string.back() == 'K' || aisaq_pq_read_page_cache_size_string.back() == 'k') {
                size_shift = 10;
            }
            if (size_shift > 0) {
                aisaq_pq_read_page_cache_size_string.pop_back();
                /* G/M/K may be set with decimal point */
                if (!std::regex_match(aisaq_pq_read_page_cache_size_string, std::regex(R"(^\d+(\.\d+)?$)"))) {
                    std::cerr << "invalid pq_read_page_cache_size value." << std::endl;
                    return -1;
                }
                aisaq_search_config.pq_read_page_cache_size = (uint64_t)(atof(aisaq_pq_read_page_cache_size_string.c_str()) * (1 << size_shift));
                break;
            }
            /* Bytes, decimal point is not allowed here */
            if (!std::regex_match(aisaq_pq_read_page_cache_size_string, std::regex(R"(^\d+$)"))) {
                std::cerr << "invalid pq_read_page_cache_size value." << std::endl;
                return -1;
            }
            aisaq_search_config.pq_read_page_cache_size = (uint64_t)(atoll(aisaq_pq_read_page_cache_size_string.c_str()));
            break;
        };
        if (aisaq_search_config.pq_read_page_cache_size > (AIS_SEARCH_PQ_READ_PAGE_CACHE_MAX_DRAM_MB * (1 << 20))) {
            std::cerr << "pq_read_page_cache_size must be <= " << AIS_SEARCH_PQ_READ_PAGE_CACHE_MAX_DRAM_MB << "MiB" << std::endl;
            return -1;
        }
    }

    try
    {
        if (!query_filters.empty() && label_type == "ushort")
        {
            if (data_type == std::string("float"))
                return search_disk_index<float, uint16_t>(
                    metric, index_path_prefix, result_path_prefix, query_file, gt_file, num_threads, K, W,
                    num_nodes_to_cache, search_io_limit, Lvec, fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else if (data_type == std::string("int8"))
                return search_disk_index<int8_t, uint16_t>(
                    metric, index_path_prefix, result_path_prefix, query_file, gt_file, num_threads, K, W,
                    num_nodes_to_cache, search_io_limit, Lvec, fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else if (data_type == std::string("uint8"))
                return search_disk_index<uint8_t, uint16_t>(
                    metric, index_path_prefix, result_path_prefix, query_file, gt_file, num_threads, K, W,
                    num_nodes_to_cache, search_io_limit, Lvec, fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else
            {
                std::cerr << "Unsupported data type. Use float or int8 or uint8" << std::endl;
                return -1;
            }
        }
        else
        {
            if (data_type == std::string("float"))
                return search_disk_index<float>(metric, index_path_prefix, result_path_prefix, query_file, gt_file,
                                                num_threads, K, W, num_nodes_to_cache, search_io_limit, Lvec,
                                                fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else if (data_type == std::string("int8"))
                return search_disk_index<int8_t>(metric, index_path_prefix, result_path_prefix, query_file, gt_file,
                                                 num_threads, K, W, num_nodes_to_cache, search_io_limit, Lvec,
                                                 fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else if (data_type == std::string("uint8"))
                return search_disk_index<uint8_t>(metric, index_path_prefix, result_path_prefix, query_file, gt_file,
                                                  num_threads, K, W, num_nodes_to_cache, search_io_limit, Lvec,
                                                  fail_if_recall_below, query_filters, use_reorder_data, aisaq_search_config);
            else
            {
                std::cerr << "Unsupported data type. Use float or int8 or uint8" << std::endl;
                return -1;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cout << std::string(e.what()) << std::endl;
        diskann::cerr << "Index search failed." << std::endl;
        return -1;
    }
}