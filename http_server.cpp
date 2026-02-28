#include "engine.h"
#include "engine_state.h"
#include "engine_common.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "vendor/httplib.h"
#include "vendor/json.hpp"

using json = nlohmann::json;

// ---------- helpers ----------
static inline bool is_valid_side(uint8_t s) { return s == BUY || s == SELL; }

static inline EngineEvent make_new_order(uint64_t seq,
                                         uint64_t account_id,
                                         uint8_t side,
                                         int64_t price,
                                         int64_t qty) {
    EngineEvent ev{};
    ev.header.sequence = seq;
    ev.header.type = EventType::NEW_ORDER;
    ev.new_order.account_id = account_id;
    ev.new_order.side = side;
    ev.new_order.price = price;
    ev.new_order.quantity = qty;
    return ev;
}

static inline EngineEvent make_cancel(uint64_t seq,
                                      uint64_t order_id) {
    EngineEvent ev{};
    ev.header.sequence = seq;
    ev.header.type = EventType::CANCEL;
    ev.cancel.order_id = order_id;
    return ev;
}

static inline json account_to_json(const Account& a) {
    // __int128 -> string to be safe in JS
    auto i128_to_string = [](const __int128 v) -> std::string {
        if (v == 0) return "0";
        __int128 x = v;
        bool neg = x < 0;
        if (neg) x = -x;
        std::string s;
        while (x > 0) {
            int digit = int(x % 10);
            s.push_back(char('0' + digit));
            x /= 10;
        }
        if (neg) s.push_back('-');
        std::reverse(s.begin(), s.end());
        return s;
    };

    json j;
    j["state"] = (a.state == AccountState::ACTIVE) ? "ACTIVE" : "FROZEN";
    j["base"]["available"]  = i128_to_string(a.base.available);
    j["base"]["locked"]     = i128_to_string(a.base.locked);
    j["quote"]["available"] = i128_to_string(a.quote.available);
    j["quote"]["locked"]    = i128_to_string(a.quote.locked);
    return j;
}

int main(int argc, char** argv) {
    // ----------------------------
    // config
    // ----------------------------
    const int port = (argc >= 2) ? std::atoi(argv[1]) : 8080;

    // ----------------------------
    // engine state (heap -> no stack explosions)
    // ----------------------------
    auto* state = new EngineState{};
    zero_state(*state);

    // seed a small range of accounts for dev/testing (optional)
    // NOTE: account 0 is DUST, keep it empty.
    for (uint64_t i = 1; i < 10'000; ++i) {
        state->accounts[i].base.available  = 1'000'000'000;
        state->accounts[i].quote.available = 1'000'000'000;
    }

    MatchingEngine engine(*state);

    // ----------------------------
    // gateway sequencing + serialization
    // ----------------------------
    std::atomic<uint64_t> next_seq{1};
    std::mutex engine_mu;

    httplib::Server api;

    // Health
    api.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        json j;
        j["ok"] = true;
        j["last_sequence"] = (unsigned long long)state->last_sequence;
        res.set_content(j.dump(), "application/json");
    });

    // Place LIMIT order
    // POST /v1/orders
    // body: {"account_id": 1, "side": 0, "price": 1000000, "quantity": 5}
    api.Post("/v1/orders", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);

            uint64_t account_id = body.at("account_id").get<uint64_t>();
            uint8_t  side       = body.at("side").get<uint8_t>();
            int64_t  price      = body.at("price").get<int64_t>();
            int64_t  quantity   = body.at("quantity").get<int64_t>();

            // basic validation
            if (account_id == DUST_ACCOUNT_ID) {
                res.status = 400;
                res.set_content(R"({"error":"account_id 0 is reserved (DUST)"})", "application/json");
                return;
            }
            if (!is_valid_side(side) || quantity <= 0 || price <= 0) {
                res.status = 400;
                res.set_content(R"({"error":"invalid side/price/quantity"})", "application/json");
                return;
            }
            if (account_id >= MAX_ACCOUNTS) {
                res.status = 400;
                res.set_content(R"({"error":"account_id out of range"})", "application/json");
                return;
            }

            uint64_t seq = next_seq.fetch_add(1, std::memory_order_relaxed);
            EngineEvent ev = make_new_order(seq, account_id, side, price, quantity);

            {
                std::lock_guard<std::mutex> lock(engine_mu);
                engine.apply(ev);
            }

            // Your engine assigns order ids internally; we can expose next_order_id-1 as the created id.
            // Caveat: if the order was rejected (insufficient funds/frozen), create() is not called.
            // In your current engine.cpp, create() is called after fund-lock succeeds, so this is safe.
            uint64_t created_oid = state->orders.next_order_id - 1;

            json out;
            out["sequence"] = (unsigned long long)seq;
            out["order_id"] = (unsigned long long)created_oid;
            out["last_sequence"] = (unsigned long long)state->last_sequence;
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            json err;
            err["error"] = "bad_request";
            err["detail"] = e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // Cancel order
    // POST /v1/cancel
    // body: {"order_id": 123}
    api.Post("/v1/cancel", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            uint64_t order_id = body.at("order_id").get<uint64_t>();

            uint64_t seq = next_seq.fetch_add(1, std::memory_order_relaxed);
            EngineEvent ev = make_cancel(seq, order_id);

            {
                std::lock_guard<std::mutex> lock(engine_mu);
                engine.apply(ev);
            }

            json out;
            out["sequence"] = (unsigned long long)seq;
            out["last_sequence"] = (unsigned long long)state->last_sequence;
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception& e) {
            json err;
            err["error"] = "bad_request";
            err["detail"] = e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // Get book top (best bid/ask)
    api.Get("/v1/book", [&](const httplib::Request&, httplib::Response& res) {
        json j;

        int32_t bb = state->book.best_bid;
        int32_t ba = state->book.best_ask;

        j["best_bid_idx"] = bb;
        j["best_ask_idx"] = ba;

        j["best_bid_price"] = (bb == -1) ? nullptr : json(state->book.min_price + int64_t(bb) * TICK_SIZE);
        j["best_ask_price"] = (ba == -1) ? nullptr : json(state->book.min_price + int64_t(ba) * TICK_SIZE);

        res.set_content(j.dump(), "application/json");
    });

    // Get account balances
    // GET /v1/accounts/<id>
    api.Get(R"(/v1/accounts/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        uint64_t id = 0;
        try {
            id = std::stoull(req.matches[1].str());
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"bad account id"})", "application/json");
            return;
        }
        if (id >= MAX_ACCOUNTS) {
            res.status = 400;
            res.set_content(R"({"error":"account_id out of range"})", "application/json");
            return;
        }

        json j;
        j["account_id"] = (unsigned long long)id;
        j["account"] = account_to_json(state->accounts[id]);
        res.set_content(j.dump(), "application/json");
    });

    // simple CORS for local dev (React)
    api.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    std::printf("HTTP gateway listening on http://127.0.0.1:%d\n", port);
    api.listen("0.0.0.0", port);

    delete state;
    return 0;
}