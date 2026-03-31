#pragma once
#include <string>
#include <functional>

namespace fate {

class UIDataBinding {
public:
    using DataProvider = std::function<std::string(const std::string& path)>;

    void setProvider(DataProvider provider) { provider_ = std::move(provider); }

    std::string resolve(const std::string& templateStr) const;
    std::string getValue(const std::string& path) const;

private:
    DataProvider provider_;
};

} // namespace fate
