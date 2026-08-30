#ifndef __FP_INFERENCE_H__
#define __FP_INFERENCE_H__

#include "src/commons.h"
#include "src/utils.h"
#include "src_fp/fp.h"

#include <cmath>
#include <stdexcept>

namespace fp_inference {

using json = nlohmann::json;

// ------------------------- fixed-point primitives -------------------------

inline Fp from_real(float real) {
    const double scaled = (double) real * (double) (1LL << FXPSCALE);
    const int64_t value = static_cast<int64_t>(std::floor(scaled));
    Fp::check((__int128) value, "from_real");
    return Fp(value);
}

inline Fp dot(const vector<Fp>& a, const vector<Fp>& b, const Fp& bias) {
    __int128 acc = 0;
    for (size_t i = 0; i < a.size(); i++) {
        acc += a[i].wide() * b[i].wide();
    }

    const int64_t wrapped = Fp::check(acc, "inference dot (untruncated accumulator)");
    const int64_t truncated = static_cast<int64_t>((__int128) wrapped >> FXPSCALE);
    return Fp::raw(truncated) + bias;
}

inline Fp relu(const Fp& x) { return x < 0 ? Fp::raw(0) : x; }


enum class Kind { AFFINE, RELU, CONV2D };

struct Layer {
    Kind kind;

    size_t inputs = 0;    // affine / conv2d: m (per output row)
    size_t outputs = 0;   // affine / conv2d: n (rows); relu: size

    vector<Fp> weights;   // row-major, outputs x inputs
    vector<Fp> biases;    // outputs

    // conv2d geometry
    int in_channels = 0, out_channels = 0;
    int image_h = 0, image_w = 0;
    int kernel_h = 0, kernel_w = 0;
    int stride_h = 1, stride_w = 1;
    int pad_h = 0, pad_w = 0;
    int out_h = 0, out_w = 0;

    // Parameters<T> layout: all n*m weights first (row-major), then the n biases.
    size_t num_params() const { return outputs * (inputs + 1); }

    void set_parameters(const vector<float>& raw) {
        weights.resize(outputs * inputs);
        biases.resize(outputs);
        for (size_t i = 0; i < outputs * inputs; i++) weights[i] = from_real(raw[i]);
        for (size_t i = 0; i < outputs; i++) biases[i] = from_real(raw[outputs * inputs + i]);
    }
};

inline vector<Fp> forward_affine(const Layer& layer, const vector<Fp>& x) {
    if (x.size() != layer.inputs) {
        throw std::runtime_error("affine layer input size mismatch");
    }

    vector<Fp> out(layer.outputs);
    vector<Fp> row(layer.inputs);
    for (size_t i = 0; i < layer.outputs; i++) {
        for (size_t j = 0; j < layer.inputs; j++) row[j] = layer.weights[i * layer.inputs + j];
        out[i] = dot(row, x, layer.biases[i]);
    }
    return out;
}

inline vector<Fp> forward_conv2d(const Layer& layer, const vector<Fp>& x) {
    if (x.size() != (size_t) (layer.in_channels * layer.image_h * layer.image_w)) {
        throw std::runtime_error("conv2d layer input size mismatch");
    }

    vector<Fp> out((size_t) layer.out_channels * layer.out_h * layer.out_w);

    for (int oc = 0; oc < layer.out_channels; oc++) {
        for (int oh = 0; oh < layer.out_h; oh++) {
            for (int ow = 0; ow < layer.out_w; ow++) {
                vector<Fp> patch, kernel;
                for (int ic = 0; ic < layer.in_channels; ic++) {
                    for (int kh = 0; kh < layer.kernel_h; kh++) {
                        for (int kw = 0; kw < layer.kernel_w; kw++) {
                            const int ih = oh * layer.stride_h - layer.pad_h + kh;
                            const int iw = ow * layer.stride_w - layer.pad_w + kw;
                            if (ih < 0 || ih >= layer.image_h || iw < 0 || iw >= layer.image_w) continue;

                            const int x_index = ic * layer.image_h * layer.image_w + ih * layer.image_w + iw;
                            const int w_index = ic * layer.kernel_h * layer.kernel_w + kh * layer.kernel_w + kw;

                            patch.push_back(x[x_index]);
                            kernel.push_back(layer.weights[(size_t) oc * layer.inputs + w_index]);
                        }
                    }
                }

                const size_t nid = (size_t) oc * layer.out_h * layer.out_w + oh * layer.out_w + ow;
                out[nid] = dot(patch, kernel, layer.biases[oc]);
            }
        }
    }
    return out;
}

inline vector<Fp> forward(const vector<Layer>& layers, const vector<float>& record) {
    vector<Fp> x(record.size());
    for (size_t i = 0; i < record.size(); i++) x[i] = from_real(record[i]);

    for (const Layer& layer : layers) {
        switch (layer.kind) {
            case Kind::AFFINE: x = forward_affine(layer, x); break;
            case Kind::CONV2D: x = forward_conv2d(layer, x); break;
            case Kind::RELU:
                if (x.size() != layer.outputs) throw std::runtime_error("relu layer size mismatch");
                for (Fp& value : x) value = relu(value);
                break;
        }
    }
    return x;
}

inline int argmax(const vector<Fp>& logits) {
    int best = 0;
    for (size_t i = 1; i < logits.size(); i++) {
        if (logits[best] < logits[i]) best = (int) i;
    }
    return best;
}

inline vector<Layer> build_layers(const json& architecture) {
    if (!architecture.is_array() || architecture.empty()) {
        throw std::runtime_error("architecture must be a non-empty array");
    }

    vector<Layer> layers;
    for (const auto& spec : architecture) {
        const std::string type = spec.at("type").get<std::string>();

        if (type == "input" || type == "output") continue;

        Layer layer;
        if (type == "affine") {
            layer.kind = Kind::AFFINE;
            layer.inputs = spec.at("inputs").get<size_t>();
            layer.outputs = spec.at("outputs").get<size_t>();
        } else if (type == "relu") {
            layer.kind = Kind::RELU;
            layer.inputs = layer.outputs = spec.at("size").get<size_t>();
        } else if (type == "conv2d") {
            layer.kind = Kind::CONV2D;
            layer.in_channels = spec.at("in_channels").get<int>();
            layer.out_channels = spec.at("out_channels").get<int>();
            layer.image_h = spec.at("image_height").get<int>();
            layer.image_w = spec.at("image_width").get<int>();
            layer.kernel_h = spec.at("kernel_height").get<int>();
            layer.kernel_w = spec.at("kernel_width").get<int>();
            layer.stride_h = spec.at("stride_height").get<int>();
            layer.stride_w = spec.at("stride_width").get<int>();
            layer.pad_h = spec.value("padding_height", 0);
            layer.pad_w = spec.value("padding_width", 0);
            layer.out_h = (layer.image_h + 2 * layer.pad_h - layer.kernel_h) / layer.stride_h + 1;
            layer.out_w = (layer.image_w + 2 * layer.pad_w - layer.kernel_w) / layer.stride_w + 1;
            layer.inputs = (size_t) layer.in_channels * layer.kernel_h * layer.kernel_w;
            layer.outputs = (size_t) layer.out_channels;
        } else {
            throw std::runtime_error("Unknown layer type: " + type);
        }

        layers.push_back(std::move(layer));
    }
    return layers;
}

inline void read_params(vector<Layer>& layers, const std::string& params_file) {
    size_t layer_offset = 0;
    for (Layer& layer : layers) {
        if (layer.kind == Kind::RELU) continue;

        const size_t num_params = layer.num_params();
        vector<float> raw(num_params);
        read_next_elements(num_params, raw.data(), layer_offset, params_file.c_str());
        layer.set_parameters(raw);
        layer_offset += num_params;
    }
}

}  

#endif
