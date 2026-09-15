#pragma once

#include <cstdio>

// Host: stderr. On ESP-IDF, replace the body with ESP_LOGI(tag, fmt, ##__VA_ARGS__).
#ifndef ZB_RPC_LOG
#define ZB_RPC_LOG(tag, fmt, ...) std::fprintf(stderr, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#endif
