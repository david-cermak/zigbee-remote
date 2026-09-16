#pragma once

#include <cstdio>

#ifndef ZB_RPC_LOG
#define ZB_RPC_LOG(tag, fmt, ...) std::fprintf(stderr, "%s: " fmt "\n", tag, ##__VA_ARGS__)
#endif
