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
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::cout << "Start time: " << std::ctime(&now_c);

    auto start = std::chrono::steady_clock::now(); 

    if constexpr (TYPE_EQ(T, IntFp)){
        for (int i = 0; i < threads; ++i){
            ios[i] = new BoolIO<NetIO>(
                new NetIO(party == ALICE ? nullptr : "127.0.0.1", port + i),
                party == ALICE
            );
        }

        start = std::chrono::steady_clock::now();

        setup_plain_prot(false, "");
        setup_zk_arith<BoolIO<NetIO>>(ios, threads, party);

        
        init_verification();
        startComputation(party);
    }




    const string INPUT_FILE_PATH = "data/inputs/credit_test.txt";
    const string PARAMS_FILE_PATH = "data/params/credit_relu_2_50.txt";

    set<int> sensitive_attrs{2};

    Model<T>* model = new Model<T>();
    model->add_layer(new Input<T>(14));
    model->add_layer(new Affine<T>(14, 8));
    model->add_layer(new ReLU<T>(8));
    model->add_layer(new Affine<T>(8, 2));
    model->add_layer(new ReLU<T>(2));
    model->add_layer(new Affine<T>(2, 2));
    model->add_layer(new Output<T>(2));

    model->read_params(PARAMS_FILE_PATH.c_str());
    cout << "Params loaded...\n";

    int num_layers = model->layers.size();

    int example_from = atoi(argv[2]), example_to = atoi(argv[3]);
    cerr << example_from << " " << example_to << "\n";

    NUM_VERIFIED = 0;
    for(int i = example_from; i <= example_to; i++){
        vector<float> input = load_input(INPUT_FILE_PATH.c_str(), (i-1) * 25, 24);
        int gt = (int) input.back();    input.pop_back();
        
        ((Output<T>*) model->layers[num_layers-1])->set_output(gt);

        cout << i << ": ";
        model->forward(input, 0.05, sensitive_attrs);
        model->reset();
    }

    cout << NUM_VERIFIED << " examples verified.\n";

    delete model;

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

    auto stop = std::chrono::steady_clock::now();

    // 3. Calculate the difference (duration)
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);

    // 4. Print the result using .count()
    std::cout << "Time elapsed: " << duration.count() << " microseconds" << std::endl;

    

    return 0;
}