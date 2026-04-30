#ifndef __UTILS_H__
#define __UTILS_H__

#include "commons.h"
#include "json.hpp"

#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <sys/types.h>


void read_next_elements(size_t n, float* buffer, size_t offset, const char* filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::string("Failed to open file: ") + filepath);
    }

    if (buffer == NULL){
        buffer = new float[n];
    }

    size_t pos = 0;
    while (file >> buffer[pos]) {
        if(offset > 0){
            offset--;
            continue;
        }

        pos++;

        if(pos == n){
            break;
        }
    }
}

float reveal_field_element_after_scaling(IntFp x){
    uint64_t clt_x = x.reveal();
    
    int64_t signed_clt_x = (clt_x >= (PR - 1)/2) ? -(PR - clt_x) : clt_x;
    
    return (signed_clt_x * 1.0) / (1LL << FXPSCALE);
}

uint64_t cleartext_inner_product_over_field(int sz, IntFp* x, IntFp* y){
    uint64_t res = 0, tmp;
    for(int i = 0; i < sz; i++){
        tmp = mult_mod(HIGH64(x[i].value), HIGH64(y[i].value));
        res = add_mod(res, tmp);
    }
    return res;
}


IntFp inner_product_bundle(int sz, IntFp* x, IntFp* y, int party){
    /*
        sz: number of actual elements in vectors x and y
        x, y: original vectors consisting of sz authenticated values
    */

    IntFp* vec = new IntFp[2*sz + 2];
    for(int i = 0; i < sz; i++){
        vec[i] = x[i];
        vec[i+sz+1] = y[i];
    }

    uint64_t res = 0;
    if(party == ALICE){
        res = cleartext_inner_product_over_field(sz, x, y);
    } 
    
    vec[sz] = FIELD_MINUS_ONE;
    vec[2*sz + 1] = IntFp(res, ALICE);

    fp_zkp_inner_prdt<BoolIO<NetIO>>(vec, vec + sz + 1, 0, sz + 1);


    IntFp ip_res = vec[2*sz + 1];
    delete[] vec;

    return ip_res;
}


// using namespace std;
// using namespace emp;
// using json = nlohmann::json;


// enum SPEC_LABELS{
//     LAYER_TYPE_INDEX = 0,
//     INPUT_SIZE_INDEX = 1,
//     OUTPUT_SIZE_INDEX = 2,
//     MAX_COEFFS_INDEX = 3,

//     INPUT_CHANNELS = 1,
//     OUTPUT_CHANNELS = 2,
//     IMAGE_H = 3,
//     IMAGE_W = 4,
//     KERNEL_H = 5,
//     KERNEL_W = 6,
//     STRIDE_H = 7,
//     STRIDE_W = 8,
//     PAD_H = 9,
//     PAD_W = 10,
//     CONV_MAX_COEFFS_INDEX = 11
// };

// template <typename Interval>
// Model<Interval>* create_model(int num_layers, int* layer_specs){
//     int specs = 0;

//     Model<Interval>* model = new Model<Interval>();

//     Layer<Interval>* layer;
//     for(int i = 0; i < num_layers; i++){

//         switch (layer_specs[specs + LAYER_TYPE_INDEX]){
//             case LAYER_TYPE::INPUT:
//                 layer = new Input<Interval>(
//                     layer_specs[specs + INPUT_SIZE_INDEX]
//                 );
//                 specs += 1;
//                 break;

//             case LAYER_TYPE::AFFINE:
//                 layer = new Affine<Interval>(
//                     layer_specs[specs + INPUT_SIZE_INDEX],
//                     layer_specs[specs + OUTPUT_SIZE_INDEX]
//                 );
//                 specs += 2;

//                 break;

//             case LAYER_TYPE::CONV2D:
//                 layer = new Conv2D<Interval>(
//                     layer_specs[specs + INPUT_CHANNELS],
//                     layer_specs[specs + OUTPUT_CHANNELS],
//                     layer_specs[specs + IMAGE_H],
//                     layer_specs[specs + IMAGE_W],
//                     layer_specs[specs + KERNEL_H],
//                     layer_specs[specs + KERNEL_W],
//                     layer_specs[specs + STRIDE_H],
//                     layer_specs[specs + STRIDE_W],
//                     layer_specs[specs + PAD_H],
//                     layer_specs[specs + PAD_W]
//                 );
//                 specs += 10;

//                 break;

//             case LAYER_TYPE::RELU:
//                 layer = new ReLU<Interval>(
//                     layer_specs[specs + INPUT_SIZE_INDEX]
//                 );
//                 specs += 1;

//                 break;

//             // case LAYER_TYPE::SIGMOID:
//             //     layer = new Sigmoid<Interval>(
//             //         layer_specs[specs + INPUT_SIZE_INDEX],
//             //         layer_specs[specs + OUTPUT_SIZE_INDEX],
//             //         layer_specs[specs + MAX_COEFFS_INDEX],
//             //         party
//             //     );
//             //     specs += 4;

//             //     break;

//             // case LAYER_TYPE::TANH:
//             //     layer = new Tanh<Interval>(
//             //         layer_specs[specs + INPUT_SIZE_INDEX],
//             //         layer_specs[specs + OUTPUT_SIZE_INDEX],
//             //         layer_specs[specs + MAX_COEFFS_INDEX],
//             //         party
//             //     );
//             //     specs += 4;

//             //     break;

//             case LAYER_TYPE::OUTPUT:
//                 layer = new Output<Interval>(
//                     layer_specs[specs + INPUT_SIZE_INDEX]
//                     party
//                 );
//                 specs += 1;

//                 break;

//             default:
//                 error("Invalid Layer type!\n");
//         }

//         model->add_layer(layer);
//     }

//     return model;
// }

// LAYER_TYPES stringToLayerType(const std::string& type) {
//     if (type == "INPUT") return INPUT;
//     if (type == "AFFINE") return AFFINE;
//     if (type == "RELU") return RELU;
//     if (type == "SIGMOID") return SIGMOID;
//     if (type == "TANH") return TANH;
//     if (type == "OUTPUT") return OUTPUT;
//     if (type == "CONV2D") return CONV2D;
//     throw std::runtime_error("Unknown layer type: " + type);
// }

// vector<int> read_exp_specs(
//     const char* config_file_path,
//     float* epsilon,
//     string &INPUT_FILE_PATH,
//     string &PARAMS_FILE_PATH,
//     string &LOG_FILE_PATH,
//     int* test_mode,
//     vector<int>* test_examples,
//     int worker_id
// ){

//     std::ifstream file(config_file_path);
//     if (!file.is_open()) {
//         std::cerr << "Failed to open" << config_file_path << std::endl;
//         return {};
//     }
    
//     json config;
//     file >> config;
//     file.close();

//     string model_name = config["model_name"];
//     if(model_name.compare(0, 5, "mnist") == 0){
//         CURR_DATASET = DATASETS::MNIST;
//         if(model_name.find("conv") != std::string::npos){
//             INPUT_FILE_PATH = "test/ai/data/inputs/mnist_conv_" + to_string(worker_id) + ".txt";
//             cerr << INPUT_FILE_PATH << "\n";
//         } else {
//             INPUT_FILE_PATH = "test/ai/data/inputs/mnist_test_" + to_string(worker_id) + ".txt";
//         }

//     } else if(model_name.compare(0, 5, "cifar") == 0) {
//         CURR_DATASET = DATASETS::CIFAR10;
//         INPUT_FILE_PATH = "test/ai/data/inputs/cifar10_test_nonconv_" + to_string(worker_id) + ".txt";

//     } else if(model_name.compare(0, 5, "adult") == 0) {
//         cerr << model_name << "\n";
//         CURR_DATASET = DATASETS::ADULT;
//         INPUT_FILE_PATH = "test/ai/data/inputs/adult_test_" + to_string(worker_id) + ".txt";

//     } else if(model_name.compare(0, 6, "credit") == 0) {
//         cerr << model_name << "\n";
//         CURR_DATASET = DATASETS::CREDIT;
//         INPUT_FILE_PATH = "test/ai/data/inputs/credit_test_" + to_string(worker_id) + ".txt";

//     } else if(model_name.compare(0, 6, "german") == 0) {
//         cerr << model_name << "\n";
//         CURR_DATASET = DATASETS::GERMAN;
//         INPUT_FILE_PATH = "test/ai/data/inputs/german_test_" + to_string(worker_id) + ".txt";

//     } else {
//         CURR_DATASET = DATASETS::TOY;
//         INPUT_FILE_PATH = "test/ai/data/inputs/toy" + to_string(worker_id) + ".txt";
//     }

//     int num_neurons = config["num_neurons"];
//     std::vector<int> layer_specs;

//     int num_layers = 0;
//     for (const auto& layer : config["layers"]) {
//         std::string type = layer[0];
//         layer_specs.push_back(stringToLayerType(type));
        
//         if(!strcmp(type.c_str(), "CONV2D")){
//             int num_specs_in_conv = 12;
//             for(int i = 1; i < num_specs_in_conv; i++){
//                 layer_specs.push_back(layer[i]);
//             }

//         } else {
//             // Handle input_size
//             if (layer[1].is_string() && layer[1] == "num_neurons") {
//                 layer_specs.push_back(num_neurons);
//             } else {
//                 layer_specs.push_back(layer[1]);
//             }
            
//             // Handle output_size
//             if (layer[2].is_string() && layer[2] == "num_neurons") {
//                 layer_specs.push_back(num_neurons);
//             } else {
//                 layer_specs.push_back(layer[2]);
//             }
            
//             layer_specs.push_back(layer[3]);
//         }
        
//         num_layers++;
//     }

//     *epsilon = (float) config["epsilon"];


//     PARAMS_FILE_PATH = "test/ai/data/parameters/" + model_name + "_" + to_string(worker_id) + ".txt";


//     string folder = "test/ai/data/logs/" + model_name;
//     struct stat st;
//     if (stat(folder.c_str(), &st)) {
//         mkdir(folder.c_str(), 0755) == 0;
//     }
//     LOG_FILE_PATH = folder + "/" + model_name;
    
//     *test_mode = (int) config["do_float"];

//     FXPSCALE = (int) config["FXPSCALE"];
//     INPUT_MIN = (float) config["input_min"];
//     INPUT_MAX = (float) config["input_max"];

//     layer_specs.push_back(num_layers);
//     cerr << "Num layers: " << layer_specs.back() << "\n";

//     vector<int> examples = config["examples"];
//     if(examples.size() > 0){
//         for(int e : examples){
//             test_examples->push_back(e);
//         }
//     }

//     if(config.contains("bs_mode")){
//         BS_MODE = config["bs_mode"];
//         if(BS_MODE == 1){
//             DO_DP_BS = true;
//         } else {
//             DO_DP_BS = false;
//         }
//     }

//     if(config.contains("sensitive_attrs")){
//         sensitive_attrs.clear();
//         vector<int> sens_attr = config["sensitive_attrs"];
//         for(int i : sens_attr){
//             sensitive_attrs.insert(i);
//         }
//     }

//     return layer_specs;
// }


// template <typename Interval>
// std::pair<bool, bool> verify_example(VerifiableFeedForwardNeuralNetwork<Interval>* model, const char* input_file, int input_offset, float epsilon, int example_num = 1){
//     model->reset();
//     model->load_input(input_file, input_offset, epsilon);
//     auto result = model->forward(example_num, true, true);
//     return result;
// }   


#endif