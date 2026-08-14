//
// lager - library for functional interactive c++ programs
// Copyright (C) 2017 Juan Pedro Bolivar Puente
//
// This file is part of lager.
//
// lager is free software: you can redistribute it and/or modify
// it under the terms of the MIT License, as detailed in the LICENSE
// file located at the root of this source code distribution,
// or here: <https://github.com/arximboldi/lager/blob/master/LICENSE>
//

#include <catch.hpp>

#include <lager/state.hpp>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {

// Two redundant representations of the same information.  A snapshot taken
// while the value is being assigned over can see the members in different
// states, which `is_intact` detects without having to rely on the heap
// corruption that the race also causes.
struct payload
{
    std::vector<std::string> items;
    std::size_t total = 0;
    char fill         = 'a';
};

// The shapes deliberately differ between successive values so that assigning
// one over another reallocates instead of overwriting in place.
payload make_payload(int n)
{
    auto items = std::vector<std::string>{};
    auto fill  = static_cast<char>('a' + n % 26);
    auto count = static_cast<std::size_t>(n % 7) + 1;
    auto total = std::size_t{0};
    for (auto i = std::size_t{0}; i < count; ++i) {
        auto size = (static_cast<std::size_t>(n % 11) + i + 1) * 16;
        items.emplace_back(size, fill);
        total += size;
    }
    return {std::move(items), total, fill};
}

bool is_intact(const payload& p)
{
    auto total = std::size_t{0};
    for (auto&& item : p.items) {
        if (item.empty() || item.front() != p.fill || item.back() != p.fill)
            return false;
        total += item.size();
    }
    return total == p.total;
}

} // namespace

TEST_CASE("state, reading from other threads while committing")
{
    constexpr auto commit_count = 50000;
    constexpr auto reader_count = 4;

    auto st = lager::make_state(make_payload(0), lager::automatic_tag{});

    auto stop     = std::atomic<bool>{false};
    auto torn     = std::atomic<int>{0};
    auto observed = std::atomic<int>{0};

    auto readers = std::vector<std::thread>{};
    for (auto i = 0; i < reader_count; ++i) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                auto snapshot = payload{st.get()};
                // Catch2 assertion macros are not thread safe, so failures are
                // counted here and checked on the main thread.
                if (!is_intact(snapshot))
                    torn.fetch_add(1, std::memory_order_relaxed);
                observed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto i = 1; i <= commit_count; ++i)
        st.set(make_payload(i));

    stop.store(true, std::memory_order_relaxed);
    for (auto&& r : readers)
        r.join();

    CHECK(observed.load() > 0);
    CHECK(torn.load() == 0);
}
