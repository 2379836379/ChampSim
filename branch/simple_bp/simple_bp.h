// branch/simple_bp/simple_bp.h
#ifndef BRANCH_SIMPLE_BP_H
#define BRANCH_SIMPLE_BP_H
#include <cstdint>
#include "address.h"
#include "modules.h"
struct simple_bp : champsim::modules::branch_predictor {
    using champsim::modules::branch_predictor::branch_predictor;
    bool predict_branch(champsim::address ip);
    void last_branch_result(champsim::address ip, champsim::address branch_target, bool taken, uint8_t branch_type);
};
#endif