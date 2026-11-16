// branch/simple_bp/simple_bp.cc
#include "simple_bp.h"
// Predict whether a branch is taken (1) or not taken (0)
bool simple_bp::predict_branch(champsim::address ip) {
// Always predict 'taken'
    return true;
}
// Update predictor state using the actual branch outcome from the trace
void simple_bp::last_branch_result(champsim::address ip, champsim::address 
    branch_target, bool taken, uint8_t branch_type) {
// Static predictor does not update any state.
}