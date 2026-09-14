#include "src/model.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>

int threads = 1;
int port = 10000;

using T = IntFp;
using json = nlohmann::json;

using namespace std;

std::map<string, string> sens_attr_names;
std::map<string, vector<string>> sens_values_semantic_names;


namespace {

void init_verification() {
    FIELD_ZERO = IntFp(0, PUBLIC);
    FIELD_ONE = IntFp(1, PUBLIC);
    FIELD_SCALED_ONE = IntFp(1ULL << FXPSCALE, PUBLIC);
    FIELD_MINUS_ONE = IntFp(PR - 1, PUBLIC);
}

json read_config(std::string config_name) {
    std::string path = project_path("data/configs/" + config_name + ".json");
    std::ifstream file(path.c_str());
    if (!file) throw std::runtime_error("Cannot open config: " + std::string(path));
    json config;
    file >> config;
    return config;
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
    localtime_r(&now, &local_time);
    std::ostringstream result;
    result << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S %Z");
    return result.str();
}

vector<float> load_input(const std::string& path, size_t example_index, size_t feature_count) {
    if (example_index < 1) {
        throw std::runtime_error("Example indices are 1-based, so 0 is not a valid index");
    }

    const size_t record_size = feature_count + 1;  // ground-truth label + features
    const size_t offset = (example_index - 1) * record_size;

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

    sens_attr_names["adult"] = "gender";
    sens_attr_names["credit"] = "gender";
    sens_attr_names["german"] = "foreign_worker";

    sens_values_semantic_names["adult"] = {"female", "male"};
    sens_values_semantic_names["credit"] = {"male", "female"};
    sens_values_semantic_names["german"] = {"yes", "no"};


    return name;
}

size_t count_neurons(const json& layers) {
    size_t neurons = 0;
    for (const auto& spec : layers) {
        const std::string type = spec.at("type").get<std::string>();
        if (type == "affine") {
            neurons += spec.at("outputs").get<size_t>();
        } else if (type == "conv2d") {
            size_t out_height = (((spec.at("image_height").get<size_t>() - spec.at("kernel_height").get<size_t>())/spec.at("stride_height").get<size_t>())+1);
            neurons += spec.at("out_channels").get<size_t>() * out_height * out_height;
        }
    }
    return neurons;
}

Model<T> build_model(const json& layers) {
    if (!layers.is_array() || layers.empty()) {
        throw std::runtime_error("architecture must be a non-empty array");
    }

    Model<T> model;
    for (const auto& spec : layers) {
        const std::string type = spec.at("type").get<std::string>();
        if (type == "input") {
            model.add_layer(new Input<T>(spec.at("size").get<size_t>()));
        } else if (type == "affine") {
            model.add_layer(new Affine<T>(spec.at("inputs").get<size_t>(),
                                          spec.at("outputs").get<size_t>()));
        } else if (type == "relu") {
            model.add_layer(new ReLU<T>(spec.at("size").get<size_t>()));
        } else if (type == "conv2d") {
            model.add_layer(new Conv2D<T>(
                spec.at("in_channels").get<int>(), spec.at("out_channels").get<int>(),
                spec.at("image_height").get<int>(), spec.at("image_width").get<int>(),
                spec.at("kernel_height").get<int>(), spec.at("kernel_width").get<int>(),
                spec.at("stride_height").get<int>(), spec.at("stride_width").get<int>(),
                spec.value("padding_height", 0), spec.value("padding_width", 0)));
        } else if (type == "output") {
            model.add_layer(new Output<T>(spec.at("size").get<size_t>()));
        } else {
            throw std::runtime_error("Unknown layer type: " + type);
        }
    }

    if (model.layers.front()->type != LAYER_TYPES::INPUT ||
        model.layers.back()->type != LAYER_TYPES::OUTPUT) {
        throw std::runtime_error("architecture must start with input and end with output");
    }
    return model;
}

}  

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <party: 1|2> <experiment.json>\n";
        return 1;
    }

    std::chrono::_V2::steady_clock::time_point wall_start;

    try {
        party = std::stoi(argv[1]);
        if (party != ALICE && party != BOB) {
            throw std::runtime_error("party must be ALICE (1) or BOB (2)");
        }

        const json config = read_config(argv[2]);
        threads = config.value("threads", 1);
        port = config.value("port", 10000);

        const std::string input_file = project_path(config.at("input_file").get<std::string>());
        const std::string params_file = project_path(config.at("params_file").get<std::string>());
        const float delta = config.at("delta").get<float>();
        const size_t feature_count = config.at("input_features").get<size_t>();
        const auto examples = config.at("example_indices").get<vector<size_t>>();
        if (examples.size() == 0){
            std::cerr << "At least one example is required, check config" << "\n";
            return 1;
        }

        const int sensitive_attribute = config.at("sensitive_attribute").get<int>();
        if (sensitive_attribute < 0 || static_cast<size_t>(sensitive_attribute) >= feature_count) {
            throw std::runtime_error("sensitive_attribute is outside the feature range");
        }
        const auto sensitive_attribute_values =
            config.value("sensitive_attribute_values", vector<float>{0.0f, 1.0f});
        if (sensitive_attribute_values.size() < 2) {
            throw std::runtime_error("sensitive_attribute_values needs at least two values");
        }

        const set<int> sensitive_attributes{sensitive_attribute};
        const size_t neurons = count_neurons(config.at("architecture"));
        const std::string dataset = dataset_name(input_file);

        std::cout << "---------------------- Fairness Proof (" << (party == 1 ? "PROVER" : "VERIFIER") << ") ------------------------\n";
        std::cout << "Dataset: " << dataset_name(input_file) << '\n';
        std::cout << "Model  : " << argv[2] << '\n';
        std::cout << "Neurons: " << neurons << '\n';
        std::cout << "Delta  : " << delta << std::endl;
        std::cout << "Sensitive attribute: " << sens_attr_names[dataset] << std::endl;

        BoolIO<NetIO>* ios[1];
        ios[0] = new BoolIO<NetIO>(new NetIO(party == ALICE ? nullptr : "127.0.0.1", port), party == ALICE);

        wall_start = std::chrono::steady_clock::now();
        std::cout << "Proof start time: " << timestamp() << '\n';

        setup_plain_prot(false, "");
        setup_zk_arith<BoolIO<NetIO>>(ios, threads, party);
        init_verification();
        startComputation(party);

        Model<T> model = build_model(config.at("architecture"));
        model.read_params(params_file.c_str());

        NUM_VERIFIED = 0;
        size_t num_fair = 0;
        auto* output = static_cast<Output<T>*>(model.layers.back());
        // Only the proof cost is measured, so the proof covers just the first example.
        const size_t index = examples.front();
        vector<float> record = load_input(input_file, index, feature_count);
        const int ground_truth = static_cast<int>(record.back());
        record.pop_back();

        output->set_output(ground_truth);

        bool fair = true; 
        int sens_attr_value_index = 0;
        for (const float value : sensitive_attribute_values) {
            record[sensitive_attribute] = value;

            std::cout << "Example " << index << "[" << sens_attr_names[dataset] << " = " << sens_values_semantic_names[dataset][sens_attr_value_index] << "] ";
            model.forward(record, delta, sensitive_attributes);
            fair = fair && output->verified;
            model.reset();

            sens_attr_value_index++;
        }

        if (fair) num_fair++;
        std::cout << "Fair: " << (fair ? "YES" : "NO") << "\n";

        endComputation(party);
        const bool cheated = finalize_zk_arith<BoolIO<NetIO>>();
        if (party == BOB) {
            std::cout << "\n" << (cheated ? "\033[31mVerification failed!" : "\033[32mVerification successful!") << "\033[0m\n";
        }

        const std::chrono::_V2::steady_clock::time_point wall_end = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = wall_end - wall_start;

        std::cout << "Proof end time: " << timestamp() << '\n';
        std::cout << "End-to-End Proof Time: " << std::fixed << std::setprecision(3) << elapsed.count() << " seconds\n";

        NetIO* net = ios[0]->io;
        delete ios[0];
        delete net;
    } catch (const std::exception& error) {
        std::cerr << "Experiment failed: " << error.what() << '\n';
        return 1;
    }

    
    return 0;
}
