#ifndef __COMMONS_H__
#define __COMMONS_H__

#include <stdlib.h>
#include <vector>
#include <map>
#include <string>
#include <type_traits>
#include <assert.h>
#include <iostream>
#include <fstream>

#include "emp-tool/emp-tool.h"
#include "emp-zk/emp-zk.h"
#include "emp-zk-math/ZKmath-functions.h"


#define TYPE_EQ(S, T) std::is_same_v<S, T>

#define elif else if

#define NOT(x) (FIELD_ONE + (x).negate())

#define CLT(x) (reveal_field_element_after_scaling(x))

int party;

size_t GLOBAL_NOISE_SYMBOL_CTR = 0;

size_t NUM_CLASSIFIED = 0;
size_t NUM_VERIFIED = 0;

size_t FXPSCALE = 24;

IntFp FIELD_ZERO;
IntFp FIELD_ONE;
IntFp FIELD_SCALED_ONE;
IntFp FIELD_MINUS_ONE;

uint64_t ZERO_COMP_CONSTANT = (PR+1)/2;

enum LAYER_TYPES{
    INPUT,
    AFFINE,
    CONV2D,
    RELU,
    SIGMOID,
    TANH,
    OUTPUT
};

enum DATASETS{MNIST, CIFAR10, ADULT, CREDIT, GERMAN};
int NUM_FEATURES[] = {784, 3072, 14, 23, 20};
int CURR_DATASET = DATASETS::MNIST;
bool DEBUG = false;

#endif