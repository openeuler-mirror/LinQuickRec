#ifndef LINQUICKREC_CONSTANT_H
#define LINQUICKREC_CONSTANT_H

namespace LinQuickRec {

    const std::unordered_map <std::string, int64_t> kActionScore = {
            {"is_click",         1},
            {"is_like",          2},
            {"is_follow",        4},
            {"is_comment",       8},
            {"is_forward",       16},
            {"is_hate",          32},
            {"long_view",        64},
            {"is_profile_enter", 128},
    };

    const std::vector <std::string> kUserActiveDegree = {
            "UNKNOWN", "day_new", "2_14_day_new", "single_low_active",
            "low_active", "middle_active", "high_active", "full_active", "30day_retention"};
    const std::vector <std::string> kFollowRange = {
            "0", "(0,10]", "(10,50]", "(50,100]", "(100,150]", "(150,250]", "(250,500]", "500+"};
    const std::vector <std::string> kFansRange = {
            "0", "[1,10)", "[10,100)", "[100,1k)", "[1k,5k)", "[5k,1w)", "[1w,10w)", "[10w,100w)", "[100w,1000w)"};
    const std::vector <std::string> kFriendRange = {
            "0", "[1,5)", "[5,30)", "[30,60)", "[60,120)", "[120,250)", "250+"};
    const std::vector <std::string> kRegisterRange = {
            "8-14", "15-30", "31-60", "61-90", "91-180", "181-365", "366-730", "730+"};


}

#endif //LINQUICKREC_CONSTANT_H
