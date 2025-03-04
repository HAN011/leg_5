#include "uart.hpp"

namespace bsp {
// 定义静态成员变量
std::unordered_map<UART_HandleTypeDef*, uart*> uart::uart_instances_ = [] {
    std::unordered_map<UART_HandleTypeDef*, uart*> map;
    map.reserve(3);
    return map;
}();
} // namespace bsp
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size) {
    bsp::uart* instance = bsp::uart::get_instance(huart);
    if (instance) {
        instance->SetTrueRxSize(Size);
        instance->OnRxCpltCallback();
    }
    instance->ReceiveDMAAuto();
}
extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    bsp::uart* instance = bsp::uart::get_instance(huart);
    instance->ReceiveDMAAuto();
}