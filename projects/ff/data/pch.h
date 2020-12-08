#pragma once

// C++
#include <cassert>
#include <charconv>
#include <filesystem>
#include <functional>
#include <memory>
#include <ostream>
#include <shared_mutex>
#include <sstream>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Windows
#define NOMINMAX
#include <Windows.h>

// FF
#include <ff/base/public_api.h>
