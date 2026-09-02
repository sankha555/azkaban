// ---------------------------------------------------------------------------
// test_small_fp.cpp
//
// Plaintext run of the zonotope verifier over the checked prime-field type `Fp`
// (p = 2^61 - 1).  It reproduces the fixed-point reference computation but
// reduces every value into the field and flags any operation whose true value
// leaves the representable signed range [-(p-1)/2, (p-1)/2] -- i.e. any place
// where the real ZK prover would silently wrap (a positive value turning
// negative, or vice-versa).
//
// Run (single process, party must be ALICE=1 so params/inputs are read):
//     ./bin/test_small_fp 1 <example_from> <example_to>
//
// A field-overflow report is printed at the end.
// ---------------------------------------------------------------------------

#include "src/cleartext/model.h"

int threads = 1;
int port = 10000;

using T = Fp;

vector<float> load_input(const char* input_file_path, int input_offset = 0, int input_size = 1){
    float gt = 0;
    read_next_elements(1, &gt, input_offset++, input_file_path);

    vector<float> input(input_size);
    read_next_elements(input_size, input.data(), input_offset, input_file_path);

    input.push_back(gt);

    return input;
}

int main(int argc, char** argv){

    if(argc < 4){
        cerr << "usage: " << argv[0] << " <party(=1)> <example_from> <example_to>"
             << " [abort_on_overflow(0/1)]\n";
        return 1;
    }

    // party is only used to gate reading of params/inputs (party == ALICE).
    // Run this plaintext checker as ALICE (1) so the weights/inputs are loaded.
    party = atoi(argv[1]);
    cerr << "party " << party << "\n";

    if(argc >= 5){
        FP_OVERFLOW.abort_on_overflow = (atoi(argv[4]) != 0);
    }

    const string INPUT_FILE_PATH  = "data/inputs/cifar_test.txt";
    const string PARAMS_FILE_PATH = "data/params/cifar_relu_mlp14_1.txt";

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 8, 32, 32, 4, 4, 2, 2, 0, 0));
    // model->add_layer(new ReLU<T>(1800));
    // model->add_layer(new Conv2D<T>(8, 8, 15, 15, 3, 3, 2, 2, 0, 0));
    // model->add_layer(new ReLU<T>(392));
    // model->add_layer(new Affine<T>(392, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 16, 32, 32, 4, 4, 2, 2, 0, 0));
    // model->add_layer(new ReLU<T>(3600));
    // model->add_layer(new Conv2D<T>(16, 32, 15, 15, 4, 4, 2, 2, 0, 0));
    // model->add_layer(new ReLU<T>(1152));
    // model->add_layer(new Affine<T>(1152, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 22, 32, 32, 5, 5, 2, 2, 0, 0));    // -> 22x14x14 = 4312
    // model->add_layer(new ReLU<T>(4312));
    // model->add_layer(new Conv2D<T>(22, 24, 14, 14, 3, 3, 3, 3, 0, 0));   // -> 24x4x4   = 384
    // model->add_layer(new ReLU<T>(384));
    // model->add_layer(new Conv2D<T>(24, 30, 4, 4, 3, 3, 1, 1, 0, 0));     // -> 30x2x2   = 120
    // model->add_layer(new ReLU<T>(120));
    // model->add_layer(new Affine<T>(120, 16));
    // model->add_layer(new ReLU<T>(16));
    // model->add_layer(new Affine<T>(16, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(784));
    // model->add_layer(new Affine<T>(784, 50));
    // model->add_layer(new ReLU<T>(50));
    // model->add_layer(new Affine<T>(50, 50));
    // model->add_layer(new ReLU<T>(50));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(50, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Affine<T>(3072, 45));   // layer 1  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 2  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 3  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 4  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 5  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 6  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 7  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 45));     // layer 8  (w=45)
    // model->add_layer(new ReLU<T>(45));
    // model->add_layer(new Affine<T>(45, 40));     // layer 9  (w=28, 29->28)
    // model->add_layer(new ReLU<T>(40));
    // model->add_layer(new Affine<T>(40, 10));     // output projection
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));

    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Affine<T>(3072, 34));   // layer 1  (w=34)
    // model->add_layer(new ReLU<T>(34));
    // model->add_layer(new Affine<T>(34, 34));     // layer 2  (w=34)
    // model->add_layer(new ReLU<T>(34));
    // model->add_layer(new Affine<T>(34, 34));     // layer 3  (w=34)
    // model->add_layer(new ReLU<T>(34));
    // model->add_layer(new Affine<T>(34, 34));     // layer 4  (w=34)
    // model->add_layer(new ReLU<T>(34));
    // model->add_layer(new Affine<T>(34, 33));     // layer 5  (w=33, 34->33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 6  (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 7  (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 8  (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 9  (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 10 (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 11 (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 33));     // layer 12 (w=33)
    // model->add_layer(new ReLU<T>(33));
    // model->add_layer(new Affine<T>(33, 10));     // output projection
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));


    // 4-hidden-layer MLP, 400 hidden nodes (320 + 28 + 26 + 26), ~17 min ZK cost.
    //
    // Cost driver: input.h gives every pixel its OWN noise symbol, so the input
    // zonotope is diagonal -- in layer 1 the noise_symbols.count(k) test in
    // affine.h matches exactly one expression, so A.size() == 1 and the layer
    // costs only w1 * 2 * 3072.  Layer 1 is therefore nearly FREE and can be
    // wide.  But affine.h then writes new_noise_symbols[k] for every k < CTR
    // (densification, so the ReLU branch pattern stays private), so from layer
    // 2 on every expression carries all ~3100-3450 symbols and each layer costs
    // w_i * w_{i-1} * D.  The bill is the sum of ADJACENT WIDTH PRODUCTS, not
    // the width of the layer touching the 3072 inputs.
    //
    // Hence: spend the node budget on a wide first layer (a rich 320-dim
    // projection of the image, bought almost for free) and keep the deeper
    // layers narrow, where each node is ~3100x more expensive.
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Affine<T>(3072, 320));   // layer 1  (w=320)  ~2.0M mults
    // model->add_layer(new ReLU<T>(320));
    // model->add_layer(new Affine<T>(320, 28));     // layer 2  (w=28)  ~27.5M mults
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 26));      // layer 3  (w=26)   ~2.5M mults
    // model->add_layer(new ReLU<T>(26));
    // model->add_layer(new Affine<T>(26, 26));      // layer 4  (w=26)   ~2.3M mults
    // model->add_layer(new ReLU<T>(26));
    // model->add_layer(new Affine<T>(26, 10));      // output projection ~0.9M mults
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));

    Model<T>* model = new Model<T>();
    model->add_layer(new Input<T>(3072));
    model->add_layer(new Affine<T>(3072, 300));    // layer 1  (w=20)   ~10.3 min
    model->add_layer(new ReLU<T>(300));
    model->add_layer(new Affine<T>(300, 34));     // layer 2  (w=128)  ~0.5 min
    model->add_layer(new ReLU<T>(34));
    model->add_layer(new Affine<T>(34, 33));    // layer 3  (w=128)  ~2.9 min
    model->add_layer(new ReLU<T>(33));
    model->add_layer(new Affine<T>(33, 33));    // layer 4  (w=124)  ~3.0 min
    model->add_layer(new ReLU<T>(33));
    model->add_layer(new Affine<T>(33, 10));     // output projection ~0.2 min
    model->add_layer(new ReLU<T>(10));
    model->add_layer(new Output<T>(10));


    model->read_params(PARAMS_FILE_PATH.c_str());
    cout << "Params loaded...\n";

    int num_layers = model->layers.size();

    int example_from = atoi(argv[2]), example_to = atoi(argv[3]);
    cerr << example_from << " " << example_to << "\n";

    NUM_VERIFIED = 0;
    // for(int i = example_from; i <= example_to; i++){
    set<int> examples = {14, 16, 19, 20, 22, 24, 27, 29, 30, 31, 33, 34, 35, 39, 40, 41, 42, 45, 46, 50, 51, 52, 54, 55, 56, 57, 61, 62, 65, 66, 67, 69, 72, 73, 74, 75, 76, 77, 78, 79, 80, 82, 83, 85, 89, 90, 91, 93, 94, 95, 98, 100};
    for(int i : examples){
        vector<float> input = load_input(INPUT_FILE_PATH.c_str(), (i-1) * 3073, 3072);
        int gt = (int) input.back();    input.pop_back();

        ((Output<T>*) model->layers[num_layers-1])->set_output(gt);

        cout << i << ": ";
        model->forward(input, 0.0002, set<int>());
        model->reset();
    }

    cout << NUM_VERIFIED << " examples verified.\n";

    // ----------------------- field overflow report -----------------------
    cout << "\n==== FIELD OVERFLOW REPORT (p = 2^61 - 1) ====\n";
    cout << "representable signed range : [-(p-1)/2, (p-1)/2] = [-"
         << fp_i128_to_string((__int128) Fp::POS_MAX) << ", "
         << fp_i128_to_string((__int128) Fp::POS_MAX) << "]\n";
    cout << "field-relevant ops checked : " << FP_OVERFLOW.checks << "\n";
    cout << "overflow events            : " << FP_OVERFLOW.events << "\n";
    if(FP_OVERFLOW.events){
        cout << "first overflow at operation: " << FP_OVERFLOW.first_op << "\n";
        cout << "RESULT: OVERFLOW DETECTED -- the field p = 2^61-1 is too small "
                "for this computation (values wrap / change sign).\n";
    } else {
        cout << "RESULT: no overflow -- every value stayed within the field.\n";
    }

    delete model;
    return FP_OVERFLOW.events ? 2 : 0;
}
