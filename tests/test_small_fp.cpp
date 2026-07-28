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

#include "src_fp/model.h"

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
    const string PARAMS_FILE_PATH = "data/params/cifar_relu_conv_small_1.txt";

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

    Model<T>* model = new Model<T>();
    model->add_layer(new Input<T>(3072));
    model->add_layer(new Conv2D<T>(3, 22, 32, 32, 5, 5, 2, 2, 0, 0));    // -> 22x14x14 = 4312
    model->add_layer(new ReLU<T>(4312));
    model->add_layer(new Conv2D<T>(22, 24, 14, 14, 3, 3, 3, 3, 0, 0));   // -> 24x4x4   = 384
    model->add_layer(new ReLU<T>(384));
    model->add_layer(new Conv2D<T>(24, 16, 4, 4, 3, 3, 1, 1, 0, 0));     // -> 16x2x2   = 64
    model->add_layer(new ReLU<T>(64));
    model->add_layer(new Affine<T>(64, 32));
    model->add_layer(new ReLU<T>(32));
    model->add_layer(new Affine<T>(32, 10));
    model->add_layer(new ReLU<T>(10));
    model->add_layer(new Affine<T>(10, 10));
    model->add_layer(new Output<T>(10));

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

    model->read_params(PARAMS_FILE_PATH.c_str());
    cout << "Params loaded...\n";

    int num_layers = model->layers.size();

    int example_from = atoi(argv[2]), example_to = atoi(argv[3]);
    cerr << example_from << " " << example_to << "\n";

    NUM_VERIFIED = 0;
    // for(int i = example_from; i <= example_to; i++){
    set<int> examples = {3, 4, 10, 11, 12, 14, 15, 17, 18, 21, 22, 26, 34, 35, 45, 46, 49, 51, 55, 57, 61, 67, 75, 81, 82, 83, 84, 91, 98, 100};
    for(int i : examples){
        vector<float> input = load_input(INPUT_FILE_PATH.c_str(), (i-1) * 3073, 3072);
        int gt = (int) input.back();    input.pop_back();

        ((Output<T>*) model->layers[num_layers-1])->set_output(gt);

        cout << i << ": ";
        model->forward(input, 0.002, set<int>());
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
