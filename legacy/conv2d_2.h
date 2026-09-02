#ifndef __CONV2D_H__
#define __CONV2D_H__

#include "layer.h"
#include "params.h"

template <typename T>
class Conv2D : public Layer<T> {
public:
    Parameters<T>* params;

    size_t in_channels, out_channels;
    size_t input_h, input_w;
    size_t kernel_h, kernel_w;
    size_t stride_h, stride_w;
    size_t pad_h, pad_w;

    size_t output_h, output_w;

    Conv2D(size_t in_c, size_t out_c,
           size_t in_h, size_t in_w,
           size_t k_h, size_t k_w,
           size_t s_h, size_t s_w,
           size_t p_h, size_t p_w) {

        in_channels = in_c;
        out_channels = out_c;
        input_h = in_h;
        input_w = in_w;
        kernel_h = k_h;
        kernel_w = k_w;
        stride_h = s_h;
        stride_w = s_w;
        pad_h = p_h;
        pad_w = p_w;

        this->type = LAYER_TYPES::CONV2D;

        output_h = (input_h + 2 * pad_h - kernel_h) / stride_h + 1;
        output_w = (input_w + 2 * pad_w - kernel_w) / stride_w + 1;

        this->input_size = in_channels * input_h * input_w;
        this->output_size = out_channels * output_h * output_w;
    }

    size_t set_parameters(const char* PARAMS_FILE_PATH, size_t layer_offset) {
        size_t num_params = out_channels * (in_channels * kernel_h * kernel_w + 1);

        std::vector<float> read_buffer(num_params);
        if (party == ALICE) {
            read_next_elements(num_params, read_buffer.data(), layer_offset, PARAMS_FILE_PATH);
        }

        vector<Interval<T>> params_itvl(num_params);
        for (size_t i = 0; i < num_params; i++) {
            params_itvl[i] = Interval<T>::intervalize_from_real(read_buffer[i]);
        }

        this->params = new Parameters<T>(
            out_channels,
            in_channels * kernel_h * kernel_w,
            params_itvl.data()
        );

        return num_params;
    }

    inline size_t input_index(size_t ic, size_t ih, size_t iw) {
        return ic * (input_h * input_w) + ih * input_w + iw;
    }

    inline size_t weight_index(size_t ic, size_t kh, size_t kw) {
        return ic * (kernel_h * kernel_w) + kh * kernel_w + kw;
    }

    void forward(Layer<T>* prev_layer) {

        for (size_t oc = 0; oc < out_channels; oc++) {
            for (size_t oh = 0; oh < output_h; oh++) {
                for (size_t ow = 0; ow < output_w; ow++) {

                    Interval<T> b = this->params->params_matrix[oc][in_channels * kernel_h * kernel_w];
                    Interval<T> new_center = b;

                    map<size_t, Interval<T>> new_noise_symbols;

                    vector<Interval<T>> A, B;

                    // -------- CENTER COMPUTATION --------
                    for (size_t ic = 0; ic < in_channels; ic++) {
                        for (size_t kh = 0; kh < kernel_h; kh++) {
                            for (size_t kw = 0; kw < kernel_w; kw++) {

                                int ih = (int)oh * stride_h + kh - pad_h;
                                int iw = (int)ow * stride_w + kw - pad_w;

                                if (ih < 0 || ih >= (int)input_h || iw < 0 || iw >= (int)input_w)
                                    continue;

                                size_t in_idx = input_index(ic, ih, iw);
                                size_t w_idx = weight_index(ic, kh, kw);

                                A.push_back(this->params->params_matrix[oc][w_idx]);
                                B.push_back(prev_layer->expressions[in_idx]->center);
                            }
                        }
                    }

                    Interval<T> ip = Interval<T>::inner_product(A.size(), A, B);
                    new_center += ip;

                    // -------- NOISE PROPAGATION --------
                    for (size_t k = 0; k < GLOBAL_NOISE_SYMBOL_CTR; k++) {
                        A.clear();
                        B.clear();

                        for (size_t ic = 0; ic < in_channels; ic++) {
                            for (size_t kh = 0; kh < kernel_h; kh++) {
                                for (size_t kw = 0; kw < kernel_w; kw++) {

                                    int ih = (int)oh * stride_h + kh - pad_h;
                                    int iw = (int)ow * stride_w + kw - pad_w;

                                    if (ih < 0 || ih >= (int)input_h || iw < 0 || iw >= (int)input_w)
                                        continue;

                                    size_t in_idx = input_index(ic, ih, iw);
                                    size_t w_idx = weight_index(ic, kh, kw);

                                    if (prev_layer->expressions[in_idx]->noise_symbols.count(k)) {
                                        B.push_back(prev_layer->expressions[in_idx]->noise_symbols[k]);
                                        A.push_back(this->params->params_matrix[oc][w_idx]);
                                    }
                                }
                            }
                        }

                        Interval<T> coeff = Interval<T>::inner_product(A.size(), A, B);
                        new_noise_symbols[k] = coeff;
                    }

                    Zonotope<T>* new_zono = new Zonotope<T>(new_center, new_noise_symbols);
                    this->expressions.push_back(new_zono);
                }
            }
        }
    }
};

#endif