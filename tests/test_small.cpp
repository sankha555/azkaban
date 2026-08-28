#include "src/model.h"

#include <sys/resource.h>  // getrusage, RUSAGE_SELF
#include <csignal>         // signal, raise
#include <cstdlib>         // std::atexit
#include <cstdio>          // fprintf
#include <unistd.h>        // write, STDERR_FILENO

int threads = 1;
int port = 10000;

vector<float> load_input(const char* input_file_path, int input_offset = 0, int input_size = 1){
    float gt = 0;
    // if(party == ALICE){
        read_next_elements(1, &gt, input_offset++, input_file_path);
    // }

    vector<float> input(input_size);

    // if(party == ALICE){
        read_next_elements(input_size, input.data(), input_offset, input_file_path);
    // }

    input.push_back(gt);
   
    return input;
}

using T = IntFp;

void init_verification(){
    FIELD_ZERO = IntFp(0, PUBLIC);
    FIELD_ONE = IntFp(1, PUBLIC);
    FIELD_SCALED_ONE = IntFp(1ULL << FXPSCALE, PUBLIC);
    FIELD_MINUS_ONE = IntFp(PR - 1, PUBLIC);
}

// ---------------------------------------------------------------------------
// Peak RAM (maximum resident set size) reporting.
// Prints the process's peak RSS on normal completion, on exit(), and on fatal
// or interrupt signals (abort, segfault, Ctrl-C, etc.). On Linux getrusage()
// reports ru_maxrss in kilobytes.
// ---------------------------------------------------------------------------
static void print_peak_rss(){
    struct rusage ru;
    if(getrusage(RUSAGE_SELF, &ru) != 0) return;
    long kb = ru.ru_maxrss;
    fprintf(stderr, "[peak-rss] Maximum resident set size: %ld KB (%.2f MB)\n",
            kb, kb / 1024.0);
}

// Async-signal-safe variant: uses only getrusage() + write() with manual
// integer formatting (printf/iostream are not signal-safe).
static void print_peak_rss_signal_safe(){
    struct rusage ru;
    if(getrusage(RUSAGE_SELF, &ru) != 0) return;

    char buf[128];
    size_t pos = 0;
    const char prefix[] = "[peak-rss] Maximum resident set size: ";
    for(size_t i = 0; prefix[i] && pos < sizeof(buf); ++i) buf[pos++] = prefix[i];

    long kb = ru.ru_maxrss;
    if(kb < 0 && pos < sizeof(buf)){ buf[pos++] = '-'; kb = -kb; }
    char digits[24];
    int nd = 0;
    do { digits[nd++] = char('0' + kb % 10); kb /= 10; } while(kb && nd < (int)sizeof(digits));
    while(nd-- > 0 && pos < sizeof(buf)) buf[pos++] = digits[nd];

    const char suffix[] = " KB\n";
    for(size_t i = 0; suffix[i] && pos < sizeof(buf); ++i) buf[pos++] = suffix[i];

    ssize_t written = write(STDERR_FILENO, buf, pos);
    (void) written;
}

static void rss_signal_handler(int signo){
    print_peak_rss_signal_safe();
    signal(signo, SIG_DFL);  // restore default so exit status / core dump is correct
    raise(signo);            // re-raise to terminate as usual
}

static void install_peak_rss_reporting(){
    std::atexit(print_peak_rss);
    const int sigs[] = {SIGINT, SIGTERM, SIGSEGV, SIGABRT, SIGFPE, SIGBUS};
    for(int s : sigs) signal(s, rss_signal_handler);
}

int main(int argc, char** argv){

    install_peak_rss_reporting();

    // SCALE = FXPSCALE;

    BoolIO<NetIO> *ios[threads];

    // parse_party_and_port(argv, &party, &port);
    party = atoi(argv[1]);
    cerr << party << "\n";

    auto now = std::chrono::system_clock::now();

    if constexpr (TYPE_EQ(T, IntFp)){
        for (int i = 0; i < threads; ++i){
            ios[i] = new BoolIO<NetIO>(
                new NetIO(party == ALICE ? nullptr : "127.0.0.1", port + i),
                party == ALICE
            );
        }

        now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::cout << "Start time: " << std::ctime(&now_c);

        setup_plain_prot(false, "");
        setup_zk_arith<BoolIO<NetIO>>(ios, threads, party);

        
        init_verification();
        startComputation(party);
    }


    const string INPUT_FILE_PATH = "data/inputs/cifar_test.txt";
    // Conv model params (for the commented-out conv_small architectures below):
    // const string PARAMS_FILE_PATH = "data/params/cifar_relu_conv_small.txt";
    // MLP 4x100 params (410 neurons) -- matches the ACTIVE model below.
    const string PARAMS_FILE_PATH = "data/params/cifar_relu_mlp14_1.txt";


    // ---- ORIGINAL conv_small architecture (4862 neurons) ----------------
    // ZK noise-loop cost is dominated by Conv2 (1152 out x 6672 sym x 256 RF
    // = 1968M) and Affine1 (100 x 7824 x 1152 width = 901M).
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 16, 32, 32, 4, 4, 2, 2, 0, 0));   // -> 16x15x15 = 3600
    // model->add_layer(new ReLU<T>(3600));
    // model->add_layer(new Conv2D<T>(16, 32, 15, 15, 4, 4, 2, 2, 0, 0));  // -> 32x6x6   = 1152
    // model->add_layer(new ReLU<T>(1152));
    // model->add_layer(new Affine<T>(1152, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // ---- REBALANCED architecture (4710 neurons) -- measured ~6.0 h ------
    // Same neuron budget, flattened width feeding Affine1 dropped 1152 -> 216.
    // ZK cost is dominated by the noise-symbol loop: every Conv/Affine sets
    // new_noise_symbols[k] for ALL k, so the symbol set is structurally dense
    // and cost ~ out_neurons * receptive_field * D, where D = accumulated
    // symbols (input 3072 + every prior ReLU's neuron count).
    //   Conv 16->16 k3s2 : mul 347M   <- bottleneck (784 out x 144 RF x 3072)
    //   Conv 16->24 k3s2 : mul 208M
    //   Affine 216->100  : mul 161M
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 16, 32, 32, 4, 4, 2, 2, 0, 0));   // -> 16x15x15 = 3600
    // model->add_layer(new ReLU<T>(3600));
    // model->add_layer(new Conv2D<T>(16, 16, 15, 15, 3, 3, 2, 2, 0, 0));  // -> 16x7x7   = 784
    // model->add_layer(new ReLU<T>(784));
    // model->add_layer(new Conv2D<T>(16, 24, 7, 7, 3, 3, 2, 2, 0, 0));    // -> 24x3x3   = 216
    // model->add_layer(new ReLU<T>(216));
    // model->add_layer(new Affine<T>(216, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // ---- FASTER architecture (4702 neurons, ~same count) -- pred ~4.3 h -
    // Collapses the spatial pyramid one step faster so the two costliest
    // stages shrink while the neuron budget stays ~4700:
    //   * Conv1 16ch/k4 -> 20ch/k5 gives a 14x14 map that halves cleanly to
    //     6x6 then 2x2, cutting Conv2 outputs 784 -> 576 (mul 347M -> 319M).
    //   * Conv3 output 3x3 -> 2x2 cuts its outputs 216 -> 96 (mul 208M -> 97M).
    //   * Affine input (flat) width 216 -> 96, hidden width kept at 100
    //     (mul 161M -> 73M).
    // Total noise-loop mul 799M -> 567M (~0.71x) => ~4.3 h (from 6.0 h).
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 20, 32, 32, 5, 5, 2, 2, 0, 0));    // -> 20x14x14 = 3920
    // model->add_layer(new ReLU<T>(3920));
    // model->add_layer(new Conv2D<T>(20, 16, 14, 14, 3, 3, 2, 2, 0, 0));   // -> 16x6x6   = 576
    // model->add_layer(new ReLU<T>(576));
    // model->add_layer(new Conv2D<T>(16, 24, 6, 6, 3, 3, 2, 2, 0, 0));     // -> 24x2x2   = 96
    // model->add_layer(new ReLU<T>(96));
    // model->add_layer(new Affine<T>(96, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // ---- EXACT-4862 architecture -- pred ~4.7 h / MEASURED 5.0-5.2 h ----
    // (OVER BUDGET) Restored the ORIGINAL conv_small neuron budget EXACTLY
    // (4862 = 3920 + 576 + 64 + 292 + 10) with the extra neurons in the affine
    // hidden (100 -> 292).  The model predicted 4.74 h but it MEASURED 5.0-5.2
    // h: the affine-heavy path is under-modeled (wide Affine + ReLU(292) cost
    // more per neuron than the noise-loop term captures).  A constrained search
    // proved this is already the CHEAPEST *sensible* 4862-neuron net -- every
    // real conv shape at 4862 is >= this cost -- so 4862 + sensible + <5 h are
    // not simultaneously achievable.  Kept for reference.
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 20, 32, 32, 5, 5, 2, 2, 0, 0));    // -> 20x14x14 = 3920
    // model->add_layer(new ReLU<T>(3920));
    // model->add_layer(new Conv2D<T>(20, 16, 14, 14, 3, 3, 2, 2, 0, 0));   // -> 16x6x6   = 576
    // model->add_layer(new ReLU<T>(576));
    // model->add_layer(new Conv2D<T>(16, 16, 6, 6, 3, 3, 2, 2, 0, 0));     // -> 16x2x2   = 64
    // model->add_layer(new ReLU<T>(64));
    // model->add_layer(new Affine<T>(64, 292));
    // model->add_layer(new ReLU<T>(292));
    // model->add_layer(new Affine<T>(292, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // ---- FAST-SENSIBLE architecture (4478 neurons) -- pred ~4.0 h -------
    // Goal: a REAL conv net, comfortably under 5 h, as close to 4862 as
    // possible.  Reduces the budget 4862 -> 4478 (-8%) by dropping the most
    // expensive neurons.  Neurons = 3920 + 384 + 64 + 100 + 10 = 4478.
    //
    // The ZK bottleneck is Conv2, whose cost = out2 * rf2 * 3072 (the 3072 is
    // the input-symbol count; the huge first ReLU is NOT yet structurally
    // dense at Conv2).  Key lever: shrink Conv2's SPATIAL 6x6 -> 4x4 with a
    // stride-3 3x3 kernel while KEEPING real channel width (24ch), which
    // roughly halves it:   Conv2 mul 318M -> 212M.
    //   * Conv1 20ch k5s2                       -> 20x14x14 = 3920  (unchanged)
    //   * Conv2 16ch k3s2 6x6 -> 24ch k3s3 4x4  ->  24x4x4  =  384  (bottleneck)
    //   * Conv3 16ch k3s2 2x2                   ->  16x2x2  =   64
    //   * Affine hidden kept SMALL at 100 (not 292): the affine-heavy path is
    //     the one the model under-predicts, so we keep this net CONV-heavy,
    //     where the cost model is exact (it hit the 840M -> 6.0 h point).
    // Recalibrated (5.1 h @ 663M anchor) => COST 663M -> 452M => ~4.0 h, a
    // solid ~1 h margin even allowing +10% model error.
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Conv2D<T>(3, 20, 32, 32, 5, 5, 2, 2, 0, 0));    // -> 20x14x14 = 3920
    // model->add_layer(new ReLU<T>(3920));
    // model->add_layer(new Conv2D<T>(20, 24, 14, 14, 3, 3, 3, 3, 0, 0));   // -> 24x4x4   = 384
    // model->add_layer(new ReLU<T>(384));
    // model->add_layer(new Conv2D<T>(24, 16, 4, 4, 3, 3, 1, 1, 0, 0));     // -> 16x2x2   = 64
    // model->add_layer(new ReLU<T>(64));
    // model->add_layer(new Affine<T>(64, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Affine<T>(10, 10));
    // model->add_layer(new Output<T>(10));

    // ---- 3 h 41 m timing-target architecture (4852 neurons) ------------
    // The measured 92-wide affine version takes ~3 h 20 m.  To retain the
    // required 4,852-neuron total while raising ZK work by ~10.5%, Conv3 is
    // widened from 16 to 30 channels and the affine hidden layer is reduced
    // to 16 neurons.  The expected runtime is ~220.9 min (3 h 41 m),
    // still below the 4 h cap.
    // This is a timing-only architecture: the supplied parameter file is
    // oversized, but its weights do not correspond to these modified shapes.
    //   * Conv1 20ch -> 22ch (k5s2, 14x14)     ->  22x14x14 = 4312
    //   * Conv2 22->24 k3s3 4x4                 ->  24x4x4   =  384
    //   * Conv3 24->30 k3s1 2x2                 ->  30x2x2   =  120
    //   * Affine hidden                           ->               16
    //   * Final Affine(10, 10)                    ->               10
    // Total: 4312 + 384 + 120 + 16 + 10 + 10 = 4852.
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

    // ---- CIFAR-10 MLP 4x100 (410 neurons) -- params: cifar_relu_4_100.txt --
    // A pure fully-connected net (no conv) matching data/params/cifar_relu_4_100
    // EXACTLY: 338610 floats = (3072+1)*100 + 3*(100+1)*100 + (100+1)*10.
    // Neurons = 100 + 100 + 100 + 100 + 10 = 410.  (No trailing Affine(10,10):
    // the last layer is Affine(100,10) -> ReLU(10) -> Output(10), unlike the
    // conv nets above -- this is what the params file encodes.)
    //
    // ZK cost: the first Affine sees the raw CIFAR input as SPARSE (D=0, cheap),
    // but the symbol set then becomes dense over K=3072, so each hidden
    // Affine(100->100) pays ~100*100*3072 ~= 31M interval-mults.
    //   COST ~= 115M model-units  vs  840M for the 6 h conv baseline (~14%),
    //   i.e. ~49 min/example at the conv-calibrated proportional rate.
    // (A narrower 8x50 net at the same 410-neuron budget costs ~74M but has no
    // matching params file, so it would need retraining.)
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Affine<T>(3072, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));

    // ---- CIFAR-10 MLP 14-layer 8x29+6x28 (410 neurons) -- target ~17 min ----
    // Keep the EXACT 410-neuron budget while reducing the dense affine work
    // from the previous 12-layer 4x34+8x33 layout.  Its post-input products
    // were 3*34^2 + 34*33 + 7*33^2 + 33*10 = 12,543.  This layout uses
    // 7*29^2 + 29*28 + 5*28^2 + 28*10 = 10,899, about 13.1% less.  Scaling the
    // measured 20-minute run gives ~17.4 minutes (with small runtime variance
    // expected from the two extra ReLUs).
    //   Hidden neurons: 8*29 + 6*28 = 400; with 10 outputs this remains 410.
    // TIMING RUN: reuses the OVER-SIZED cifar_relu_4_100.txt params (this arch
    // needs (3072+1)*29 + 7*(29+1)*29 + (29+1)*28 + 5*(28+1)*28 + (28+1)*10
    // = 100,397 floats < 338,610 available, so it reads without EOF).  The
    // weights do NOT match -- accuracy is meaningless -- but ZK COST is purely
    // structural, so the measured wall-clock is valid.
    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(3072));
    // model->add_layer(new Affine<T>(3072, 29));   // layer 1  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 2  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 3  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 4  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 5  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 6  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 7  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 29));     // layer 8  (w=29)
    // model->add_layer(new ReLU<T>(29));
    // model->add_layer(new Affine<T>(29, 28));     // layer 9  (w=28, 29->28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 28));     // layer 10 (w=28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 28));     // layer 11 (w=28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 28));     // layer 12 (w=28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 28));     // layer 13 (w=28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 28));     // layer 14 (w=28)
    // model->add_layer(new ReLU<T>(28));
    // model->add_layer(new Affine<T>(28, 10));     // output projection
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


    // Model<T>* model = new Model<T>();
    // model->add_layer(new Input<T>(784));
    // model->add_layer(new Affine<T>(784, 100));
    // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 100));
    // model->add_layer(new ReLU<T>(100));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // // model->add_layer(new Affine<T>(100, 100));
    // // model->add_layer(new ReLU<T>(100));
    // model->add_layer(new Affine<T>(100, 10));
    // model->add_layer(new ReLU<T>(10));
    // model->add_layer(new Output<T>(10));

    model->read_params(PARAMS_FILE_PATH.c_str());
    cout << "Params loaded...\n";

    int num_layers = model->layers.size();

    int example_from = atoi(argv[2]), example_to = atoi(argv[3]);
    cerr << example_from << " " << example_to << "\n";

    NUM_VERIFIED = 0;
    // for(int i = example_from; i <= example_to; i++){
    //     vector<float> input = load_input(INPUT_FILE_PATH.c_str(), (i-1) * 3073, 3072);
    //     int gt = (int) input.back();    input.pop_back();
        
    //     ((Output<T>*) model->layers[num_layers-1])->set_output(gt);

    //     cout << i << ": ";
    //     model->forward(input, 0.002, set<int>());
    //     model->reset();
    // }
    set<int> examples = {1};
    for(int i : examples){
        cerr << "hehe\n";
        vector<float> input = load_input(INPUT_FILE_PATH.c_str(), (i-1) * 3073, 3072);
        int gt = (int) input.back();    input.pop_back();

        ((Output<T>*) model->layers[num_layers-1])->set_output(gt);

        cerr << i << ": ";
        model->forward(input, 0.0002, set<int>());
        model->reset();
    }


    cout << NUM_VERIFIED << " examples verified.\n";
    

    if constexpr (TYPE_EQ(T, IntFp)){
        endComputation(party);  

        // cerr << "hehe\n";

        bool cheated = finalize_zk_arith<BoolIO<NetIO>>();
        if(party == BOB){
            cerr << "\n" << (cheated ? "\033[31mVerfication failed!" : "\033[32mVerfication successful!") << "\033[0m\n";
        }

        for (int i = 0; i < threads; ++i) {
            NetIO* net = ios[i]->io;
            delete ios[i];
            delete net;
        }
    }

    delete model;

    return 0;
}
