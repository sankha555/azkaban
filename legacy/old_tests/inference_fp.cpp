#include "src/cleartext/inference.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

int threads = 1;
int port = 10000;

using json = nlohmann::json;
using fp_inference::Layer;

namespace {

// ------------------------------ config / data ------------------------------

json read_config(std::string config_name) {
    std::string path = "data/configs/" + config_name + ".json";
    std::ifstream file(path.c_str());
    if (!file) throw std::runtime_error("Cannot open config: " + std::string(path));
    json config;
    file >> config;
    return config;
}

vector<float> load_input(const std::string& path, size_t example_index, size_t example_index_base, size_t feature_count) {
    if (example_index < example_index_base) {
        throw std::runtime_error("Example index is below example_index_base");
    }

    const size_t record_size = feature_count + 1;  // ground-truth label + features
    const size_t offset = (example_index - example_index_base) * record_size;

    float ground_truth = 0;
    read_next_elements(1, &ground_truth, offset, path.c_str());
    vector<float> input(feature_count);
    read_next_elements(feature_count, input.data(), offset + 1, path.c_str());
    input.push_back(ground_truth);
    return input;
}

std::string dataset_name(const std::string& input_file_path) {
    const size_t slash = input_file_path.find_last_of('/');
    const size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    size_t end = input_file_path.find_last_of('.');
    if (end == std::string::npos || end < start) end = input_file_path.size();

    std::string name = input_file_path.substr(start, end - start);
    for (const std::string& suffix : {"_test", "_train", "_val"}) {
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.erase(name.size() - suffix.size());
            break;
        }
    }
    return name;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 5) {
        std::cerr << "usage: " << argv[0] << " <experiment> [example_from example_to] [abort_on_overflow(0/1)]\n";
        return 1;
    }

    party = ALICE;
    FP_OVERFLOW.abort_on_overflow = false;

    try {
        const json config = read_config(argv[1]);

        const std::string input_file = config.at("input_file").get<std::string>();
        const std::string params_file = config.at("params_file").get<std::string>();
        const size_t feature_count = config.at("input_features").get<size_t>();
        const size_t example_index_base = config.value("example_index_base", 1U);

        vector<size_t> examples = config.at("example_indices").get<vector<size_t>>();
        if (argc >= 4) {
            const size_t from = (size_t) std::stoull(argv[2]);
            const size_t to = (size_t) std::stoull(argv[3]);
            if (to < from) throw std::runtime_error("example_to is below example_from");
            examples.clear();
            for (size_t i = from; i <= to; i++) examples.push_back(i);
        }
        if (argc == 3 || argc == 5) {
            FP_OVERFLOW.abort_on_overflow = (std::stoi(argv[argc - 1]) != 0);
        }

        std::cout << "------------------ Fixed-Point (Fp) Inference -------------------\n";
        std::cout << "Dataset: " << dataset_name(input_file) << '\n';
        std::cout << "Model: " << argv[1] << '\n';
        std::cout << "Scale: 2^" << FXPSCALE << '\n';

        vector<Layer> layers = fp_inference::build_layers(config.at("architecture"));
        fp_inference::read_params(layers, params_file);
        std::cout << "Parameters loaded from " << params_file << '\n';

        size_t num_correct = 0;
        for (const size_t index : examples) {
            vector<float> record = load_input(input_file, index, example_index_base, feature_count);
            const int ground_truth = static_cast<int>(record.back());
            record.pop_back();

            const vector<Fp> logits = fp_inference::forward(layers, record);
            const int prediction = fp_inference::argmax(logits);
            const bool correct = (prediction == ground_truth);
            num_correct += correct ? 1 : 0;

            std::cout << index << ": [";
            for (size_t i = 0; i < logits.size(); i++) {
                std::cout << (i ? ", " : "") << std::fixed << std::setprecision(6) << logits[i].to_real();
            }
            std::cout << "] prediction: " << prediction << ", ground truth: " << ground_truth
                      << " -> " << (correct ? "CORRECT" : "WRONG") << '\n';
        }

        std::cout << '\n';
        std::cout << "Examples evaluated : " << examples.size() << '\n';
        std::cout << "Accuracy           : " << std::fixed << std::setprecision(2)
                  << num_correct * 100.0 / examples.size() << "% ("
                  << num_correct << "/" << examples.size() << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "Inference failed: " << error.what() << '\n';
        return 1;
    }

    // ----------------------- field overflow report -----------------------
    std::cout << "\n==== FIELD OVERFLOW REPORT (p = 2^61 - 1) ====\n";
    std::cout << "representable signed range : [-"
              << fp_i128_to_string((__int128) Fp::POS_MAX) << ", "
              << fp_i128_to_string((__int128) Fp::POS_MAX) << "]\n";
    std::cout << "field-relevant ops checked : " << FP_OVERFLOW.checks << '\n';
    std::cout << "overflow events            : " << FP_OVERFLOW.events << '\n';
    if (FP_OVERFLOW.events) {
        std::cout << "first overflow at operation: " << FP_OVERFLOW.first_op << '\n';
        std::cout << "RESULT: OVERFLOW DETECTED -- the field p = 2^61-1 is too small "
                     "for this computation (values wrap / change sign).\n";
    } else {
        std::cout << "RESULT: no overflow -- every value stayed within the field.\n";
    }

    return FP_OVERFLOW.events ? 2 : 0;
}
