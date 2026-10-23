export module speculo;

import std;
import crab_cpp;

export import :error;
export import :modifier;
export import :descriptor;
export import :object;
export import :function;
export import :registry;

export namespace speculo
{

/**
 * @brief 返回 speculo 的版本字符串
 * @return 形如 "0.1.0" 的语义化版本号
 */
[[nodiscard]] auto version_string() -> std::string_view
{
    return "0.1.0";
}

} // namespace speculo
