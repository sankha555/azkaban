"""
Driver for the DeepZono abstract interpreter (CPU only).

Certifies L_infinity robustness of an mnist/cifar10 neural network.

Example:
    python3 test_deepzono.py --model R1 --dataset mnist --delta 0.01 --num_tests 100
"""

import argparse
import contextlib
import csv
import io
import os
import sys
import time
import warnings
from pathlib import Path

os.environ.setdefault('TF_CPP_MIN_LOG_LEVEL', '3')
os.environ.setdefault('CUDA_VISIBLE_DEVICES', '-1')
warnings.filterwarnings('ignore', message='gpupoly not available.')

# Every path below is built from the repository root, so this driver can be
# run from any directory.
ERAN_DIR = Path(__file__).resolve().parent
ROOT = ERAN_DIR.parent
MODEL_DIR = ROOT / 'data' / 'models'

sys.path.insert(0, str(ERAN_DIR / 'ELINA' / 'python_interface'))
sys.path.insert(0, str(ERAN_DIR / 'python_helpers'))

import numpy as np

from config import config
from eran import ERAN
from read_net_file import read_onnx_net, read_tensorflow_net

DOMAIN = 'deepzono'

NUM_PIXELS = {'mnist': 784, 'cifar10': 3072}

DEFAULT_NORMALIZATION = {
    'mnist':   ([0.1307000070810318], [0.30810001492500305]),
    'cifar10': ([0.4914, 0.4822, 0.4465], [0.2023, 0.1994, 0.2010]),
}


def str2bool(v):
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')


def model_path(model_name):
    """Resolve a model name (e.g. "R1") to data/models/<name>.onnx under the root."""
    name = Path(model_name).stem      # tolerate "R1.onnx" and a stray path prefix
    return str(MODEL_DIR / (name + '.onnx'))


def normalize(image, means, stds, dataset, is_conv):
    # normalization taken out of the network
    if len(means) == len(image):
        for i in range(len(image)):
            image[i] -= means[i]
            if stds is not None:
                image[i] /= stds[i]
    elif dataset == 'mnist':
        for i in range(len(image)):
            image[i] = (image[i] - means[0]) / stds[0]
    elif dataset == 'cifar10':
        tmp = np.zeros(3072)
        count = 0
        for i in range(1024):
            tmp[count] = (image[count] - means[0]) / stds[0]
            count = count + 1
            tmp[count] = (image[count] - means[1]) / stds[1]
            count = count + 1
            tmp[count] = (image[count] - means[2]) / stds[2]
            count = count + 1

        if is_conv:
            for i in range(3072):
                image[i] = tmp[i]
        else:
            count = 0
            for i in range(1024):
                image[i] = tmp[count]
                count = count + 1
                image[i + 1024] = tmp[count]
                count = count + 1
                image[i + 2048] = tmp[count]
                count = count + 1


def denormalize(image, means, stds, dataset):
    if dataset == 'mnist':
        for i in range(len(image)):
            image[i] = image[i] * stds[0] + means[0]
    elif dataset == 'cifar10':
        tmp = np.zeros(3072)
        count = 0
        for i in range(1024):
            tmp[count] = image[count] * stds[0] + means[0]
            count = count + 1
            tmp[count] = image[count] * stds[1] + means[1]
            count = count + 1
            tmp[count] = image[count] * stds[2] + means[2]
            count = count + 1

        for i in range(3072):
            image[i] = tmp[i]


def get_tests(dataset):
    if config.subset is None:
        try:
            csvfile = open(str(ERAN_DIR / 'data' / '{}_test_full.csv'.format(dataset)), 'r')
        except IOError:
            csvfile = open(str(ERAN_DIR / 'data' / '{}_test.csv'.format(dataset)), 'r')
    else:
        csvfile = open(str(ERAN_DIR / 'data' / (dataset + '_test_' + config.subset + '.csv')), 'r')
    return csv.reader(csvfile, delimiter=',')


def parse_args():
    parser = argparse.ArgumentParser(description='DeepZono analysis',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--model', type=str, default=None,
                        help='the model name (e.g. R1); resolved to data/models/<model>.onnx')
    parser.add_argument('--dataset', type=str, default=config.dataset,
                        help='the dataset, can be either mnist or cifar10')
    parser.add_argument('--delta', type=float, default=config.epsilon,
                        help='the delta for L_infinity perturbation')
    parser.add_argument('--subset', type=str, default=config.subset,
                        help='suffix of the file to specify the subset of the test dataset to use')
    parser.add_argument('--mean', nargs='+', type=float, default=config.mean,
                        help='the mean used to normalize the data with')
    parser.add_argument('--std', nargs='+', type=float, default=config.std,
                        help='the standard deviation used to normalize the data with')
    parser.add_argument('--from_test', type=int, default=config.from_test,
                        help='the index of the first image to test')
    parser.add_argument('--num_tests', type=int, default=config.num_tests,
                        help='number of images to test')
    parser.add_argument('--normalized_region', type=str2bool, default=False,
                        help='whether to clip the adversarial region to the valid pixel range before normalizing')
    parser.add_argument('--use_default_heuristic', type=str2bool, default=config.use_default_heuristic,
                        help='whether to always create new noise symbols per ReLU for the DeepZono approximation')
    parser.add_argument('--timeout_lp', type=float, default=config.timeout_lp, help='timeout for the LP solver')
    parser.add_argument('--timeout_milp', type=float, default=config.timeout_milp, help='timeout for the MILP solver')
    parser.add_argument('--debug', type=str2bool, default=config.debug, help='whether to display debug info')

    args = parser.parse_args()
    for k, v in vars(args).items():
        setattr(config, k, v)

    config.epsilon = config.delta
    assert config.model, 'a model has to be provided for analysis.'
    config.netname = model_path(config.model)
    assert os.path.isfile(config.netname), \
        'Model file not found. Please check "{}" is correct.'.format(config.netname)
    assert config.dataset in NUM_PIXELS, 'only mnist and cifar10 datasets are supported'
    return args


def load_network(netname, dataset):
    """Returns (eran, is_conv, means, stds) for the given network file."""
    _, file_extension = os.path.splitext(netname)
    is_onnx = file_extension == '.onnx'
    is_trained_with_pytorch = file_extension == '.pyt'

    quiet = contextlib.nullcontext() if config.debug else contextlib.redirect_stdout(io.StringIO())
    with quiet:
        if is_onnx:
            model, is_conv = read_onnx_net(netname)
            means, stds = None, None
        else:
            model, is_conv, means, stds = read_tensorflow_net(
                netname, NUM_PIXELS[dataset], is_trained_with_pytorch, False)

        eran = ERAN(model, is_onnx=is_onnx)

    if not is_trained_with_pytorch:
        means, stds = DEFAULT_NORMALIZATION[dataset]
    if config.mean is not None:
        means, stds = config.mean, config.std

    if config.debug:
        print("means", means, "stds", stds, "is_conv", is_conv)
    
    num_neurons = eran.optimizer.get_neuron_count()
    return eran, is_conv, means, stds, num_neurons


def main():
    parse_args()
    dataset = config.dataset
    delta = config.delta
    print("-"*10, " Floating-Point ERAN DeepZono Certification Evaluation ", "-"*10)

    cpu_affinity = os.sched_getaffinity(0)
    eran, is_conv, means, stds, num_neurons = load_network(config.netname, dataset)
    os.sched_setaffinity(0, cpu_affinity)
    
    print("Dataset: ", (dataset if dataset!="cifar10" else "cifar"), "\nModel: ", config.model, "\nNeurons: ", num_neurons, "\nDelta: ", delta, sep="")
    

    tests = get_tests(dataset)

    correctly_classified_images = 0
    verified_images = 0
    unsafe_images = 0
    cum_time = 0
    correctly_classified = []
    verified = []

    for i, test in enumerate(tests):
        if i < config.from_test:
            continue
        if config.num_tests is not None and i >= config.from_test + config.num_tests:
            break

        label = int(test[0])
        image = np.float64(test[1:len(test)]) / np.float64(255)

        # Concrete run to check whether the image is classified correctly.
        specLB = np.copy(image)
        specUB = np.copy(image)
        normalize(specLB, means, stds, dataset, is_conv)
        normalize(specUB, means, stds, dataset, is_conv)

        start = time.time()
        predicted_label, _, nlb, nub, _, _ = eran.analyze_box(
            specLB, specUB, DOMAIN, config.timeout_lp, config.timeout_milp,
            config.use_default_heuristic)

        if predicted_label != label:
            # skip certification for incorrectly classified image
            continue

        correctly_classified_images += 1
        correctly_classified.append(i + 1)

        # Certify the L_infinity ball of radius delta around the image.
        if config.normalized_region:
            specLB = np.clip(image - delta, 0, 1)
            specUB = np.clip(image + delta, 0, 1)
            normalize(specLB, means, stds, dataset, is_conv)
            normalize(specUB, means, stds, dataset, is_conv)
        else:
            specLB = specLB - delta
            specUB = specUB + delta

        perturbed_label, _, nlb, nub, _, x = eran.analyze_box(
            specLB, specUB, DOMAIN, config.timeout_lp, config.timeout_milp,
            config.use_default_heuristic, label=label
        )

        if perturbed_label == label:
            if config.debug:
                print("lower bounds:", nlb[-1])
                print("upper bounds:", nub[-1])
            verified_images += 1
            verified.append(i + 1)
        elif x is not None:
            cex_label, _, _, _, _, _ = eran.analyze_box(
                x, x, DOMAIN, config.timeout_lp, config.timeout_milp,
                config.use_default_heuristic)
            if cex_label != label:
                denormalize(x, means, stds, dataset)
                unsafe_images += 1

        cum_time += time.time() - start
        print("Progress: {}/{} examples\r".format(1 + i - config.from_test, config.num_tests), end="")

    print("\n")    
    print("Examples evaluated     : ", config.num_tests, sep="")
    print("Inference Accuracy     : ", int(round(correctly_classified_images*100/config.num_tests, 0)), "% (", correctly_classified_images, "/", config.num_tests, ")", sep="")
    print("Certification Accuracy : ", int(round(verified_images*100/correctly_classified_images, 0)), "% (", verified_images, "/", correctly_classified_images, ")", sep="")
    

if __name__ == '__main__':
    main()
