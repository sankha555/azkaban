// #ifndef __CONV2D_H__
// #define __CONV2D_H__

// #include "layer.h"
// #include "params.h"

// template <typename T>
// class Conv2D : public Layer<T> {
//     public:

//     int in_channels; 
//     int out_channels;
//     int image_h;
//     int image_w;

//     int kernel_h;
//     int kernel_w;
//     int stride_h;
//     int stride_w;
//     int pad_h;
//     int pad_w;
//     int out_h;
//     int out_w;

//     Kernel2D<T>* kernel;
//     vector<vector<int>> predecessors;
//     vector<Interval<T>> pixel_buffer;
    
//     Conv2D(
//         int in_channels, int out_channels, int image_h, int image_w, 
//         int h, int w, int stride_h, int stride_w, int pad_h, int pad_w
//     ){

//         this->in_channels = in_channels;
//         this->out_channels = out_channels;
//         this->image_h = image_h;
//         this->image_w = image_w;
//         this->kernel_h = h;
//         this->kernel_w = w;
//         this->stride_h = stride_h;
//         this->stride_w = stride_w;
//         this->pad_h = pad_h;
//         this->pad_w = pad_w;

//         this->input_size = in_channels * image_h * image_w;

//         this->set_output_image_sizes();
//         this->output_size = this->out_channels * this->out_h * this->out_w;

//         this->predecessors = vector<vector<int>>(this->output_size);
//         this->reset_predecessors();

//         this->pixel_buffer = vector<Interval<T>>(this->in_channels * this->kernel_h * this->kernel_w);

//         this->type = LAYER_TYPES::CONV2D;
//     }

//     void set_output_image_sizes(){
//         this->out_h = (int) (this->image_h + 2*this->pad_h - this->kernel_h)/this->stride_h + 1;

//         this->out_w = (int) (this->image_w + 2*this->pad_w - this->kernel_w)/this->stride_w + 1;
//     }


//     int set_parameters(const char* PARAMS_FILE_PATH, int layer_offset){
//         int num_params = out_channels * in_channels * kernel_h * kernel_w + out_channels;

//         std::vector<float> read_buffer(num_params);
//         if(party == ALICE){
//             cout << "reading params\n";
//             read_next_elements(num_params, read_buffer.data(), layer_offset, PARAMS_FILE_PATH);
//         }

//         vector<Interval<T>> params_itvl(num_params);
//         for(int i = 0; i < num_params; i++){
//             params_itvl[i] = Interval<T>::intervalize_from_real(read_buffer[i]);
//             // cout << params_itvl[i].to_string() << "\n";
//         }

//         this->kernel = new Kernel2D<T>(
//             out_channels,
//             in_channels,
//             kernel_h,
//             kernel_w,
//             params_itvl.data()
//         );

//         for(int i = 0; i < this->kernel->num_parameters(); i++){
//             // cerr << this->kernel->filter_matrix[i].to_string() << "\n";
//         }

//         return num_params;
//     }


//     void reset_predecessors(){
//         for(int z = 0; z < this->out_channels; z++){
//             for(int y = 0; y < this->out_h; y++){
//                 for(int x = 0; x < this->out_w; x++){
//                     int nid = z * this->out_h * this->out_w + y * this->out_w + x;
//                     load_predecessor_neurons(nid, stride_h * y, stride_w * x);
//                 }
//             }
//         }
//     }

//     void load_predecessor_neurons(int nid, int y, int x){

//         this->predecessors[nid].clear();
//         for(int c = 0; c < this->in_channels; c++){
//             for(int i = 0; i < this->kernel_h; i++){
//                 for(int j = 0; j < this->kernel_w; j++){

//                     int input_y = y + i - this->pad_h;
//                     int input_x = x + j - this->pad_w;

//                     if(input_y >= 0 && input_y < this->image_h && input_x >= 0 && input_x < this->image_w){

//                         this->predecessors[nid].push_back(
//                             c * this->image_h * this->image_w +
//                             input_y * this->image_w +
//                             input_x
//                         );
                    
//                     } else {

//                         this->predecessors[nid].push_back(
//                             -1
//                         );

//                     }
                    
//                 }
//             }
//         }
//     }


//     void load_flattened_pixels(Layer<T>* prev_layer, int y, int x){
//         for(int c = 0; c < this->in_channels; c++){
//             for(int i = 0; i < this->kernel_h; i++){
//                 for(int j = 0; j < this->kernel_w; j++){
//                     int input_y = y + i - this->pad_h;
//                     int input_x = x + j - this->pad_w;
                    
//                     // Check if position is within bounds
//                     if(input_y >= 0 && input_y < this->image_h && input_x >= 0 && input_x < this->image_w){
//                         // Valid pixel - load from input
//                         this->pixel_buffer[
//                             c * this->kernel_h * this->kernel_w +
//                             i * this->kernel_w +
//                             j
//                         ] = prev_layer->expressions[
//                             c * this->image_h * this->image_w +
//                             input_y * this->image_w +
//                             input_x
//                         ];
//                     } else {
//                         // Out of bounds - use zero padding
//                         this->pixel_buffer[
//                             c * this->kernel_h * this->kernel_w +
//                             i * this->kernel_w +
//                             j
//                         ] = Zonotope<T>();
//                     }
//                 }
//             }
//         }
//     }


//     void forward(Layer<T>* prev_layer) {

//         int t = 0;
//         cerr << "Conv\n";
//         // for(int c = 0; c < this->out_channels; c++){
//         //     for(int d = 0; d < this->in_channels; d++){
//         //         for(int i = 0; i < this->kernel_h; i++){
//         //             for(int j = 0; j < this->kernel_w; j++){
//         //                 cerr << this->kernel->filter_matrix[t++].to_string() << " ";
//         //             }
//         //             cerr << "\n";
//         //         }
//         //         cerr << "\n";
//         //     }
//         //     cerr << "\n"; 
//         // }

//         for(int i = 0; i < this->out_channels; i++){
//             // cerr << this->kernel->filter_matrix[this->out_channels * this->kernel->params_per_out_channel + i].to_string() << " ";
//         }

//         // exit(0);


//         int m = this->input_size;

//         this->expressions.resize(this->out_h * this->out_w * this->out_channels);

//                 for(int z = 0; z < this->out_channels; z++){

//         for(int y = 0; y < this->out_h; y++){
//             for(int x = 0; x < this->out_w; x++){

//                     // a new output neuron starts

//                     int nid = z * this->out_h * this->out_w + y * this->out_w + x;
//                     // cerr << "Neuron " << nid << "\n";

//                     vector<int> preds = this->predecessors[nid];

//                     Interval<T>* mask = this->kernel->get_flattened_weights(z);

//                     Interval<T> b = this->kernel->filter_matrix[this->out_channels * this->kernel->params_per_out_channel + z];

//                     // cerr << b.to_string() << " ";

//                     Interval<T> new_center = b;
//                     // cerr << new_center.to_string() << " ";

//                     map<size_t, Interval<T>> new_noise_symbols;

//                     vector<Interval<T>> A, B;
//                     int j = 0;
//                     for(int pred : preds){
//                         // \sum_{j = 1}^{m}{w_ij * c_j}
//                         if(pred != -1){
//                             A.push_back(mask[j]);
//                             B.push_back(prev_layer->expressions[pred]->center);
//                         }
//                         j++;
//                     }
//                     assert(A.size() == B.size());

//                     Interval<T> ip = Interval<T>::inner_product(A.size(), A, B);
//                     new_center += ip;

//                     // cerr << new_center.to_string() << "\n";

//                     for(int k = 0; k < GLOBAL_NOISE_SYMBOL_CTR; k++){
//                         A.clear();
//                         B.clear();

//                         j = 0;
//                         for(int pred : preds){
//                             // \sum_{j = 1}^{m}{w_ij * c_j}
//                             if(pred != -1 && prev_layer->expressions[pred]->noise_symbols.count(k)){
//                                 A.push_back(mask[j]);
//                                 B.push_back(prev_layer->expressions[pred]->noise_symbols[k]);
//                             }
//                             j++;
//                         }

//                         Interval<T> new_coeff = Interval<T>::inner_product(A.size(), A, B);

//                         new_noise_symbols[k] = new_coeff;
//                     }

//                     Zonotope<T>* new_zono = new Zonotope<T>(new_center, new_noise_symbols);

//                     this->expressions[nid] = new_zono;
//                 }
//             }
//         }

//         cerr << "\n";
//     }
// };

// #endif



#ifndef __CONV_2D__
#define __CONV_2D__

#include "params.h"
#include "layer.h"

template <typename T>
class Conv2D : public Layer<T> {

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

    Parameters<T>* kernel;

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

    size_t set_parameters(const char* PARAMS_FILE_PATH, size_t layer_offset){
        size_t num_params = this->out_channels * (this->in_channels * this->kernel_h * this->kernel_w + 1);

        std::vector<float> read_buffer(num_params);
        if(party == ALICE){
            cout << "reading params\n";
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

                    // traversing each output neuron in the order of out_channels -> height -> weight

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
                    this->expressions[nid] = new Zonotope(new_center, new_noise_symbols);

                    t++;
                }
            }

        }

        cerr << t << "\n";
    }
};

#endif