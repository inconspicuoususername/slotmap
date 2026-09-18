module;

#include <string>
#include <expected>
export module slotmap:error;

namespace inco {
    export template <class T>
    using ResultType = std::expected<T, std::string>;

    export auto unexpected(std::string&& str) -> std::unexpected<std::string> {
        return  std::unexpected(str);
    }
}