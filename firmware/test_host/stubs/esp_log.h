#pragma once
// Evaluate arguments too, so strict host warnings still catch mismatched code.
void test_log(const char *tag, const char *fmt, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
