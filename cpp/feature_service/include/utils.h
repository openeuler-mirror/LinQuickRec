#ifndef LINQUICKREC_UTILS_H
#define LINQUICKREC_UTILS_H

namespace LinQuickRec {
    template<typename T>
    int onehot_encode(const T& categories, const std::string& value, int default_idx = 0) {
        auto it = std::find(categories.begin(), categories.end(), value);
        return (it == categories.end()) ? default_idx : static_cast<int>(it - categories.begin());
    }
}  // namespace LinQuickRec

#endif //LINQUICKREC_UTILS_H
