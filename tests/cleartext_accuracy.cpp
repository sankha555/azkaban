#include "src/cleartext/model.h"
#include "src/cleartext/inference.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

int threads = 1;
int port = 10000;

using T = Fp;
using json = nlohmann::json;
using FpLayer = fp_inference::Layer;

namespace {

json read_config(std::string config_name) {
    std::string path = project_path("data/configs/" + config_name + ".json");
    std::ifstream file(path.c_str());
    if (!file) throw std::runtime_error("Cannot open config: " + std::string(path));
    json config;
    file >> config;
    return config;
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

Model<T>* build_model(const json& layers) {
    if (!layers.is_array() || layers.empty()) {
        throw std::runtime_error("architecture must be a non-empty array");
    }

    Model<T>* model = new Model<T>();
    for (const auto& spec : layers) {
        const std::string type = spec.at("type").get<std::string>();
        if (type == "input") {
            model->add_layer(new Input<T>(spec.at("size").get<size_t>()));
        } else if (type == "affine") {
            model->add_layer(new Affine<T>(spec.at("inputs").get<size_t>(),
                                           spec.at("outputs").get<size_t>()));
        } else if (type == "relu") {
            model->add_layer(new ReLU<T>(spec.at("size").get<size_t>()));
        } else if (type == "conv2d") {
            model->add_layer(new Conv2D<T>(
                spec.at("in_channels").get<int>(), spec.at("out_channels").get<int>(),
                spec.at("image_height").get<int>(), spec.at("image_width").get<int>(),
                spec.at("kernel_height").get<int>(), spec.at("kernel_width").get<int>(),
                spec.at("stride_height").get<int>(), spec.at("stride_width").get<int>(),
                spec.value("padding_height", 0), spec.value("padding_width", 0)));
        } else if (type == "output") {
            model->add_layer(new Output<T>(spec.at("size").get<size_t>()));
        } else {
            throw std::runtime_error("Unknown layer type: " + type);
        }
    }

    if (model->layers.front()->type != LAYER_TYPES::INPUT ||
        model->layers.back()->type != LAYER_TYPES::OUTPUT) {
        delete model;
        throw std::runtime_error("architecture must start with input and end with output");
    }
    return model;
}

} 

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: " << argv[0]
                  << " <experiment.json> [delta] [abort_on_overflow(0/1)]\n";
        return 1;
    }

    party = ALICE;

    Model<T>* model = nullptr;
    try {
        const json config = read_config(argv[1]);

        const std::string input_file = project_path(config.at("input_file").get<std::string>());
        const std::string params_file = project_path(config.at("params_file").get<std::string>());
        const size_t feature_count = config.at("input_features").get<size_t>();
        const auto examples = config.at("example_indices").get<vector<size_t>>();
        const size_t neurons = count_neurons(config.at("architecture"));

        float delta = config.at("delta").get<float>();
        if (argc >= 3) delta = std::stof(argv[2]);
        if (argc >= 4) FP_OVERFLOW.abort_on_overflow = (std::stoi(argv[3]) != 0);

        std::cout << "------------------ Cleartext Evaluation -------------------\n";
        std::cout << "Dataset: " << dataset_name(input_file) << '\n';
        std::cout << "Model:   " << argv[1] << '\n';
        std::cout << "Neurons: " << neurons << '\n';
        std::cout << "Delta:   " << delta << std::endl;

        model = build_model(config.at("architecture"));
        model->read_params(params_file.c_str());

        vector<FpLayer> inference_layers = fp_inference::build_layers(config.at("architecture"));
        fp_inference::read_params(inference_layers, params_file);

        NUM_VERIFIED = 0;
        size_t num_correct = 0;
        auto* output = static_cast<Output<T>*>(model->layers.back());
        size_t i = 0;
        for (const size_t index : examples) {
            vector<float> record = load_input(input_file, index, feature_count);
            const int ground_truth = static_cast<int>(record.back());
            record.pop_back();

            const vector<Fp> logits = fp_inference::forward(inference_layers, record);
            const int prediction = fp_inference::argmax(logits);

            if (prediction != ground_truth) {
                continue;
            }

            num_correct++;

            output->set_output(ground_truth);
            model->forward(record, delta, set<int>{});
            model->reset();

            std::cout << "Progress: " << ++i << "/" << examples.size() << " examples\r";
        }
        std::cout << std::endl;
        std::cout << '\n';
        std::cout << "Examples evaluated     : " << examples.size() << '\n';
        std::cout << "Inference Accuracy     : " << std::fixed << std::setprecision(0)
                  << num_correct * 100.0 / examples.size() << "% ("
                  << num_correct << "/" << examples.size() << ")\n";
        std::cout << "Certification Accuracy : ";
        if (num_correct == 0) {
            std::cout << "n/a (no correctly classified example to certify)\n";
        } else {
            std::cout << std::fixed << std::setprecision(0)
                      << NUM_VERIFIED * 100.0 / num_correct << "% ("
                      << NUM_VERIFIED << "/" << num_correct << ")\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Experiment failed: " << error.what() << '\n';
        delete model;
        return 1;
    }

    delete model;
    return 0;
}
