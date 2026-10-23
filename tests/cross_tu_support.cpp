import std;
import crab_cpp;
import speculo;

namespace cross_tu
{

// Defined in tests/function_test.cpp.
int local_to_main(int value);

int shared_fn(int value)
{
    return value * 3;
}

} // namespace cross_tu

/**
 * @brief Materializes both cross-TU probes from this translation unit
 *
 * `shared_fn` is defined here; `local_to_main` is defined in function_test.cpp.
 * The latter is only *declared* here, so the registry entry this creates is the
 * one the other TU looks up without ever calling function_of itself.
 */
void cross_tu_register()
{
    (void)speculo::function_of<&cross_tu::shared_fn>();
    (void)speculo::function_of<&cross_tu::local_to_main>();
}
