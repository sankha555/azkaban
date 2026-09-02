#ifndef __FP_CONV2D_H__
#define __FP_CONV2D_H__

#include "src/cleartext/layer.h"
#include "src/cleartext/params.h"

// Conv2D (mirrors the enabled class in src/conv2d.h). It only uses
// Interval<T>/Zonotope<T>/inner_product, so the field-overflow checks happen
// automatically inside Interval<Fp>.
template <typename T>
class Conv2D : public Layer<T> {
    static_assert(TYPE_EQ(T, Fp), "the cleartext backend is Fp-only");

    public:

    int in_channels;
    int out_channels;
    int image_h;
    int image_w;
    int kernel_h;
    int kernel_w;
    int stride_h;
    int stride_w;
    int pad_h;
    int pad_w;
    int out_h;
    int out_w;

    Parameters<T>* kernel = nullptr;

    Conv2D(
        int in_channels, int out_channels, int image_h, int image_w,
        int kernel_h, int kernel_w, int stride_h, int stride_w, int pad_h, int pad_w
    ) {

        this->in_channels = in_channels;
        this->out_channels = out_channels;
        this->image_h = image_h;
        this->image_w = image_w;
        this->kernel_h = kernel_h;
        this->kernel_w = kernel_w;
        this->stride_h = stride_h;
        this->stride_w = stride_w;
        this->pad_h = pad_h;
        this->pad_w = pad_w;

        this->out_h = (this->image_h + 2 * pad_h - kernel_h) / stride_h + 1;
        this->out_w = (this->image_w + 2 * pad_w - kernel_w) / stride_w + 1;

        this->type = LAYER_TYPES::CONV2D;
    }

    ~Conv2D(){
        delete this->kernel;
    }

    size_t set_parameters(const char* PARAMS_FILE_PATH, size_t layer_offset){
        size_t num_params = this->out_channels * (this->in_channels * this->kernel_h * this->kernel_w + 1);

        std::vector<float> read_buffer(num_params);
        if(party == ALICE){
            read_next_elements(num_params, read_buffer.data(), layer_offset, PARAMS_FILE_PATH);
        }

        vector<Interval<T>> params_itvl(num_params);
        for(int i = 0; i < num_params; i++){
            params_itvl[i] = Interval<T>::intervalize_from_real(read_buffer[i]);
        }

        this->kernel = new Parameters<T>(this->out_channels, this->in_channels * this->kernel_h * this->kernel_w, params_itvl.data());

        return num_params;
    }

    size_t num_weights(){
        return this->in_channels * this->kernel_h * this->kernel_w;
    }

    void forward(Layer<T>* prev_layer){

        this->expressions = vector<Zonotope<T>*>(this->out_channels * this->out_h * this->out_w);

        Interval<T> b;
        size_t t = 0;

        for(int oc = 0; oc < this->out_channels; oc++){

            b = this->kernel->params_matrix[oc][this->num_weights()];

            for(int oh = 0; oh < this->out_h; oh++){
                for(int ow = 0; ow < this->out_w; ow++){

                    Interval<T> new_center = b;
                    vector<Interval<T>> A, B;

                    for (int ic = 0; ic < in_channels; ic++) {
                        for (int kh = 0; kh < kernel_h; kh++) {
                            for (int kw = 0; kw < kernel_w; kw++) {

                                int ih = oh * stride_h - pad_h + kh;
                                int iw = ow * stride_w - pad_w + kw;

                                if(ih >= 0 && ih < image_h && iw >= 0 && iw < image_w){
                                    int x_index = ic * image_h * image_w + ih * image_w + iw;
                                    int w_index = ic * kernel_h * kernel_w + kh * kernel_w + kw;

                                    A.push_back(prev_layer->expressions[x_index]->center);
                                    B.push_back(this->kernel->params_matrix[oc][w_index]);
                                }
                            }
                        }
                    }

                    new_center += Interval<T>::inner_product(A.size(), A, B);


                    map<size_t, Interval<T>> new_noise_symbols;
                    for(size_t k = 0; k < GLOBAL_NOISE_SYMBOL_CTR; k++){

                        A.clear(); B.clear();

                        for (int ic = 0; ic < in_channels; ic++) {
                            for (int kh = 0; kh < kernel_h; kh++) {
                                for (int kw = 0; kw < kernel_w; kw++) {

                                    int ih = oh * stride_h - pad_h + kh;
                                    int iw = ow * stride_w - pad_w + kw;

                                    if(ih >= 0 && ih < image_h && iw >= 0 && iw < image_w){
                                        int x_index = ic * image_h * image_w + ih * image_w + iw;
                                        int w_index = ic * kernel_h * kernel_w + kh * kernel_w + kw;

                                        if(prev_layer->expressions[x_index]->noise_symbols.count(k)){
                                            A.push_back(prev_layer->expressions[x_index]->noise_symbols[k]);
                                            B.push_back(this->kernel->params_matrix[oc][w_index]);
                                        }
                                    }
                                }
                            }
                        }

                        Interval<T> new_coeff = Interval<T>::inner_product(A.size(), A, B);
                        new_noise_symbols[k] = new_coeff;
                    }

                    size_t nid = oc * this->out_h * this->out_w + oh * this->out_h + ow;
                    this->expressions[nid] = new Zonotope<T>(new_center, new_noise_symbols);

                    t++;
                }
            }

        }

        cerr << t << "\n";
    }
};

#endif
