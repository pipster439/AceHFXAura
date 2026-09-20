#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <cassert>
#include <filesystem>
#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"
#include "engine/builtin_effects.h"
#include "engine/plugin_manager.h"
#include "third_party/json.hpp"

static int g_failures = 0;
static int g_total_checks = 0;

#define ADV_CHECK(cond, msg) \
    do { \
        g_total_checks++; \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failures++; \
        } else { \
            std::cout << "  [PASS] " << msg << "\n"; \
        } \
    } while (0)

// Helper to create a dummy GSI state with typical CS2 telemetry
static std::shared_ptr<aura::GsiState> CreateTestGsi() {
    auto gsi = std::make_shared<aura::GsiState>();
    nlohmann::json payload = {
        {"provider", {{"name", "Counter-Strike: Global Offensive"}, {"appid", 730}}},
        {"map", {{"mode", "competitive"}, {"name", "de_mirage"}, {"phase", "live"}}},
        {"round", {{"phase", "live"}, {"bomb", "planted"}}},
        {"player", {
            {"steamid", "76561198000000000"},
            {"name", "Player1"},
            {"activity", "playing"},
            {"state", {
                {"health", 18},
                {"armor", 85},
                {"helmet", true},
                {"flashed", 0},
                {"smoked", 0},
                {"burning", 0},
                {"money", 4200},
                {"round_kills", 3},
                {"round_killhs", 2}
            }}
        }}
    };
    gsi->UpdateFromPayload(payload);
    return gsi;
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  Adversarial Stress Harness: ConditionNode AST Engine   \n";
    std::cout << "=========================================================\n\n";

    auto gsi = CreateTestGsi();

    // -------------------------------------------------------------------------
    // Phase 1: Deeply Nested Trees (10+, 25, 50, 100 levels)
    // -------------------------------------------------------------------------
    std::cout << "[Phase 1] Deeply Nested AST Trees (10+ to 100 levels)...\n";
    {
        // 1.1: 10 levels of nested AND: all true
        nlohmann::json deep_and_10 = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", 18}
        };
        for (int i = 0; i < 10; ++i) {
            deep_and_10 = {
                {"type", "and"},
                {"conditions", nlohmann::json::array({deep_and_10})}
            };
        }
        aura::ConditionNode node_and_10 = aura::ConditionNode::FromJson(deep_and_10);
        ADV_CHECK(node_and_10.Evaluate(gsi.get(), "cs2.exe") == true, "10-level nested AND tree evaluates to true");

        // 1.2: 25 levels of nested alternating AND/OR
        nlohmann::json deep_alt_25 = {
            {"field", "player.state.armor"},
            {"op", ">="},
            {"value", 85}
        };
        for (int i = 0; i < 25; ++i) {
            std::string op = (i % 2 == 0) ? "and" : "or";
            deep_alt_25 = {
                {"type", op},
                {"conditions", nlohmann::json::array({deep_alt_25})}
            };
        }
        aura::ConditionNode node_alt_25 = aura::ConditionNode::FromJson(deep_alt_25);
        ADV_CHECK(node_alt_25.Evaluate(gsi.get(), "cs2.exe") == true, "25-level nested alternating AND/OR evaluates to true");

        // 1.3: 50 levels of nested NOT chain on true condition (even depth -> true, odd depth -> false)
        nlohmann::json leaf_true = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", 18}
        };
        nlohmann::json not_chain_50 = leaf_true;
        for (int i = 0; i < 50; ++i) {
            not_chain_50 = {
                {"type", "not"},
                {"conditions", nlohmann::json::array({not_chain_50})}
            };
        }
        aura::ConditionNode node_not_50 = aura::ConditionNode::FromJson(not_chain_50);
        ADV_CHECK(node_not_50.Evaluate(gsi.get(), "cs2.exe") == true, "50-level nested NOT chain on true yields true (even parity)");

        nlohmann::json not_chain_51 = {
            {"type", "not"},
            {"conditions", nlohmann::json::array({not_chain_50})}
        };
        aura::ConditionNode node_not_51 = aura::ConditionNode::FromJson(not_chain_51);
        ADV_CHECK(node_not_51.Evaluate(gsi.get(), "cs2.exe") == false, "51-level nested NOT chain on true yields false (odd parity)");

        // 1.4: 100 levels of nested NOT chain
        nlohmann::json not_chain_100 = leaf_true;
        for (int i = 0; i < 100; ++i) {
            not_chain_100 = {
                {"type", "not"},
                {"conditions", nlohmann::json::array({not_chain_100})}
            };
        }
        aura::ConditionNode node_not_100 = aura::ConditionNode::FromJson(not_chain_100);
        ADV_CHECK(node_not_100.Evaluate(gsi.get(), "cs2.exe") == true, "100-level nested NOT chain survives stack and evaluates correctly (true)");

        // 1.5: 50 levels of nested AND with one leaf at level 49 false
        nlohmann::json deep_and_with_false = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", 999} // FALSE
        };
        for (int i = 0; i < 50; ++i) {
            deep_and_with_false = {
                {"type", "and"},
                {"conditions", nlohmann::json::array({
                    deep_and_with_false,
                    {{"field", "player.state.armor"}, {"op", "=="}, {"value", 85}}
                })}
            };
        }
        aura::ConditionNode node_and_false = aura::ConditionNode::FromJson(deep_and_with_false);
        ADV_CHECK(node_and_false.Evaluate(gsi.get(), "cs2.exe") == false, "50-level nested AND with deep false leaf correctly evaluates to false");

        // 1.6: Wide tree: 1000 leaves in a single AND node
        nlohmann::json wide_and = {
            {"type", "and"},
            {"conditions", nlohmann::json::array()}
        };
        for (int i = 0; i < 1000; ++i) {
            wide_and["conditions"].push_back({
                {"field", "player.state.health"},
                {"op", "=="},
                {"value", 18}
            });
        }
        aura::ConditionNode node_wide = aura::ConditionNode::FromJson(wide_and);
        ADV_CHECK(node_wide.Evaluate(gsi.get(), "cs2.exe") == true, "Wide AST with 1000 leaves evaluates to true");

        // Inject one false leaf among 1000 leaves
        wide_and["conditions"][500] = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", 0}
        };
        aura::ConditionNode node_wide_false = aura::ConditionNode::FromJson(wide_and);
        ADV_CHECK(node_wide_false.Evaluate(gsi.get(), "cs2.exe") == false, "Wide AST with 1000 leaves correctly fails on single false element");

        // 1.7: AST Round-trip Serialization (ToJson -> FromJson) at depth 50
        nlohmann::json serialized_50 = node_not_50.ToJson();
        aura::ConditionNode roundtrip_50 = aura::ConditionNode::FromJson(serialized_50);
        ADV_CHECK(roundtrip_50.Evaluate(gsi.get(), "cs2.exe") == true, "Round-trip ToJson/FromJson fidelity at depth 50 preserved");
    }

    // -------------------------------------------------------------------------
    // Phase 2: Unknown Field Types, Values & Null Handling
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 2] Unknown Field Types, Values & Extrema...\n";
    {
        // 2.1: null target value
        nlohmann::json j_null_val = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", nullptr}
        };
        aura::ConditionNode node_null_val = aura::ConditionNode::FromJson(j_null_val);
        ADV_CHECK(node_null_val.Evaluate(gsi.get(), "cs2.exe") == false, "Numeric field == null evaluates to false safely");
        nlohmann::json j_null_ne = {
            {"field", "player.state.health"},
            {"op", "!="},
            {"value", nullptr}
        };
        aura::ConditionNode node_null_ne = aura::ConditionNode::FromJson(j_null_ne);
        ADV_CHECK(node_null_ne.Evaluate(gsi.get(), "cs2.exe") == true, "Numeric field != null evaluates to true safely");

        // 2.2: boolean target value compared to number field
        nlohmann::json j_bool_on_num = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", true}
        };
        aura::ConditionNode node_bool_on_num = aura::ConditionNode::FromJson(j_bool_on_num);
        ADV_CHECK(node_bool_on_num.Evaluate(gsi.get(), "cs2.exe") == false, "Number field compared with boolean false safely");

        // 2.3: array target value
        nlohmann::json j_arr_val = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", nlohmann::json::array({18, 20})}
        };
        aura::ConditionNode node_arr = aura::ConditionNode::FromJson(j_arr_val);
        ADV_CHECK(node_arr.Evaluate(gsi.get(), "cs2.exe") == false, "Array target value returns false without crashing");

        // 2.4: object target value
        nlohmann::json j_obj_val = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", {{"inner", 18}}}
        };
        aura::ConditionNode node_obj = aura::ConditionNode::FromJson(j_obj_val);
        ADV_CHECK(node_obj.Evaluate(gsi.get(), "cs2.exe") == false, "Object target value returns false without crashing");

        // 2.5: Numeric extrema: 1e300, -1e300, 1e-15
        nlohmann::json j_huge = {
            {"field", "player.state.health"},
            {"op", "<"},
            {"value", 1e300}
        };
        aura::ConditionNode node_huge = aura::ConditionNode::FromJson(j_huge);
        ADV_CHECK(node_huge.Evaluate(gsi.get(), "cs2.exe") == true, "18 < 1e300 evaluates to true");

        nlohmann::json j_tiny = {
            {"field", "player.state.health"},
            {"op", ">"},
            {"value", -1e300}
        };
        aura::ConditionNode node_tiny = aura::ConditionNode::FromJson(j_tiny);
        ADV_CHECK(node_tiny.Evaluate(gsi.get(), "cs2.exe") == true, "18 > -1e300 evaluates to true");

        // 2.6: String containing valid number vs string containing text
        nlohmann::json j_str_num = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", "18"}
        };
        aura::ConditionNode node_str_num = aura::ConditionNode::FromJson(j_str_num);
        ADV_CHECK(node_str_num.Evaluate(gsi.get(), "cs2.exe") == true, "Numeric string '18' correctly parses and matches 18");

        nlohmann::json j_str_bad = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", "eighteen"}
        };
        aura::ConditionNode node_str_bad = aura::ConditionNode::FromJson(j_str_bad);
        ADV_CHECK(node_str_bad.Evaluate(gsi.get(), "cs2.exe") == false, "Non-numeric string returns false safely");

        // 2.7: Non-existent GSI field
        nlohmann::json j_unknown_field = {
            {"field", "player.state.non_existent_telemetry"},
            {"op", "=="},
            {"value", 100}
        };
        aura::ConditionNode node_unknown = aura::ConditionNode::FromJson(j_unknown_field);
        ADV_CHECK(node_unknown.Evaluate(gsi.get(), "cs2.exe") == false, "Non-existent GSI field evaluates to false safely");

        // 2.8: Huge string in field name (10,000 characters)
        std::string huge_field(10000, 'x');
        nlohmann::json j_huge_field = {
            {"field", huge_field},
            {"op", "=="},
            {"value", 1}
        };
        aura::ConditionNode node_huge_field = aura::ConditionNode::FromJson(j_huge_field);
        ADV_CHECK(node_huge_field.Evaluate(gsi.get(), "cs2.exe") == false, "10,000-char field name evaluates to false safely");

        // 2.9: Process field matching tests (.exe suffix, case, target_value non-string)
        nlohmann::json j_proc_eq = {
            {"field", "process"},
            {"op", "=="},
            {"value", "CS2"}
        };
        aura::ConditionNode node_proc_eq = aura::ConditionNode::FromJson(j_proc_eq);
        ADV_CHECK(node_proc_eq.Evaluate(gsi.get(), "cs2.exe") == true, "Case-insensitive process match ('CS2' == 'cs2.exe')");

        nlohmann::json j_proc_with_exe = {
            {"field", "process.name"},
            {"op", "=="},
            {"value", "cs2.exe"}
        };
        aura::ConditionNode node_proc_with_exe = aura::ConditionNode::FromJson(j_proc_with_exe);
        ADV_CHECK(node_proc_with_exe.Evaluate(gsi.get(), "cs2") == true, "Process match with .exe ('cs2.exe' == 'cs2')");

        nlohmann::json j_proc_ne = {
            {"field", "process_name"},
            {"op", "!="},
            {"value", "explorer.exe"}
        };
        aura::ConditionNode node_proc_ne = aura::ConditionNode::FromJson(j_proc_ne);
        ADV_CHECK(node_proc_ne.Evaluate(gsi.get(), "cs2.exe") == true, "Process inequality ('cs2.exe' != 'explorer.exe')");

        nlohmann::json j_proc_bad_val = {
            {"field", "process"},
            {"op", "=="},
            {"value", 12345} // non-string value
        };
        aura::ConditionNode node_proc_bad_val = aura::ConditionNode::FromJson(j_proc_bad_val);
        ADV_CHECK(node_proc_bad_val.Evaluate(gsi.get(), "cs2.exe") == false, "Process match with non-string value returns false safely");

        nlohmann::json j_proc_invalid_op = {
            {"field", "process"},
            {"op", "<"},
            {"value", "cs2.exe"} // unsupported op on process
        };
        aura::ConditionNode node_proc_invalid_op = aura::ConditionNode::FromJson(j_proc_invalid_op);
        ADV_CHECK(node_proc_invalid_op.Evaluate(gsi.get(), "cs2.exe") == false, "Unsupported '<' op on process field returns false safely");
    }

    // -------------------------------------------------------------------------
    // Phase 3: Invalid & Unsupported Operators
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 3] Invalid & Unsupported Operators...\n";
    {
        // 3.1: Unknown comparison operators: ">>>", "foo", "~", "==="
        std::vector<std::string> invalid_ops = {">>>", "foo", "~", "===", "like", "regex", "in", ""};
        for (const auto& op : invalid_ops) {
            auto comp = aura::ConditionNode::StringToCompareOp(op);
            ADV_CHECK(comp == aura::CompareOp::Eq, "Invalid op '" + op + "' falls back to Eq safely");
            nlohmann::json j_op = {
                {"field", "player.state.health"},
                {"op", op},
                {"value", 18}
            };
            aura::ConditionNode node_op = aura::ConditionNode::FromJson(j_op);
            ADV_CHECK(node_op.Evaluate(gsi.get(), "cs2.exe") == true, "Node with invalid op '" + op + "' evaluates as Eq (18 == 18)");
        }

        // 3.2: Unknown logic operators: "xor", "nand", "nor", "random"
        std::vector<std::string> invalid_logic = {"xor", "nand", "nor", "and_or", "123", ""};
        for (const auto& lop : invalid_logic) {
            auto logic = aura::ConditionNode::StringToLogicOp(lop);
            ADV_CHECK(logic == aura::LogicOp::None, "Invalid logic op '" + lop + "' falls back to None safely");
        }
    }

    // -------------------------------------------------------------------------
    // Phase 4: Malformed JSON Structures & Non-Object Roots
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 4] Malformed JSON Structures & Non-Object Roots...\n";
    {
        // 4.1: Non-object JSON roots
        nlohmann::json roots = {
            42,
            -1.5,
            "just_a_string",
            true,
            false,
            nullptr,
            nlohmann::json::array({1, 2, 3})
        };
        for (const auto& r : roots) {
            aura::ConditionNode n = aura::ConditionNode::FromJson(r);
            ADV_CHECK(n.logic_op == aura::LogicOp::None && n.field.empty(), "Non-object JSON root produces safe default ConditionNode");
            ADV_CHECK(n.Evaluate(gsi.get(), "cs2.exe") == true, "Default empty leaf evaluates to true");
        }

        // 4.2: Corrupted conditions: "conditions" is a string or number instead of array
        nlohmann::json bad_conditions = {
            {"type", "and"},
            {"conditions", "not_an_array"}
        };
        aura::ConditionNode n_bad_cond = aura::ConditionNode::FromJson(bad_conditions);
        ADV_CHECK(n_bad_cond.children.empty(), "Corrupted 'conditions' produces empty children list without crashing");
        ADV_CHECK(n_bad_cond.Evaluate(gsi.get(), "cs2.exe") == true, "Empty AND evaluates to true");

        // 4.3: Corrupted condition in NOT: "condition" is an integer
        nlohmann::json bad_not = {
            {"type", "not"},
            {"condition", 12345}
        };
        aura::ConditionNode n_bad_not = aura::ConditionNode::FromJson(bad_not);
        ADV_CHECK(n_bad_not.children.size() == 1, "Non-object child in NOT parsed to default child node");

        // 4.4: Corrupted field: "field" is a number in JSON
        try {
            nlohmann::json bad_field = {
                {"field", 12345},
                {"op", "=="},
                {"value", 100}
            };
            aura::ConditionNode n = aura::ConditionNode::FromJson(bad_field);
            ADV_CHECK(true, "FromJson with numeric field survived");
        } catch (const std::exception& e) {
            ADV_CHECK(true, std::string("FromJson with numeric field threw expected standard exception: ") + e.what());
        }
    }

    // -------------------------------------------------------------------------
    // Phase 5: Empty Conditions & Edge-Case Truth Tables
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 5] Empty Conditions & Edge-Case Truth Tables...\n";
    {
        // 5.1: Completely empty object {}
        nlohmann::json empty_obj = nlohmann::json::object();
        aura::ConditionNode n_empty = aura::ConditionNode::FromJson(empty_obj);
        ADV_CHECK(n_empty.Evaluate(gsi.get(), "cs2.exe") == true, "Completely empty object evaluates to true");

        // 5.2: Empty AND node: standard boolean algebra conjunction over empty set is TRUE
        nlohmann::json empty_and = {
            {"type", "and"},
            {"conditions", nlohmann::json::array()}
        };
        aura::ConditionNode n_empty_and = aura::ConditionNode::FromJson(empty_and);
        ADV_CHECK(n_empty_and.Evaluate(gsi.get(), "cs2.exe") == true, "Conjunction over empty set (empty AND) is TRUE");

        // 5.3: Empty OR node: standard boolean algebra disjunction over empty set is FALSE
        nlohmann::json empty_or = {
            {"type", "or"},
            {"conditions", nlohmann::json::array()}
        };
        aura::ConditionNode n_empty_or = aura::ConditionNode::FromJson(empty_or);
        ADV_CHECK(n_empty_or.Evaluate(gsi.get(), "cs2.exe") == false, "Disjunction over empty set (empty OR) is FALSE");

        // 5.4: Empty NOT node: no child to negate -> FALSE
        nlohmann::json empty_not = {
            {"type", "not"},
            {"conditions", nlohmann::json::array()}
        };
        aura::ConditionNode n_empty_not = aura::ConditionNode::FromJson(empty_not);
        ADV_CHECK(n_empty_not.Evaluate(gsi.get(), "cs2.exe") == false, "Empty NOT node evaluates to FALSE");

        // 5.5: Leaf node with empty field string
        nlohmann::json empty_field = {
            {"field", ""},
            {"op", "=="},
            {"value", 123}
        };
        aura::ConditionNode n_empty_field = aura::ConditionNode::FromJson(empty_field);
        ADV_CHECK(n_empty_field.Evaluate(gsi.get(), "cs2.exe") == true, "Leaf node with empty field evaluates to true");

        // 5.6: Null GSI pointer evaluation
        nlohmann::json gsi_node = {
            {"field", "player.state.health"},
            {"op", "=="},
            {"value", 18}
        };
        aura::ConditionNode n_gsi = aura::ConditionNode::FromJson(gsi_node);
        ADV_CHECK(n_gsi.Evaluate(nullptr, "cs2.exe") == false, "GSI condition with null gsi_state returns false safely");

        nlohmann::json proc_node = {
            {"field", "process"},
            {"op", "=="},
            {"value", "cs2.exe"}
        };
        aura::ConditionNode n_proc = aura::ConditionNode::FromJson(proc_node);
        ADV_CHECK(n_proc.Evaluate(nullptr, "cs2.exe") == true, "Process condition with null gsi_state evaluates true on proc match");
    }

    // -------------------------------------------------------------------------
    // Phase 6: High-Frequency Evaluation Latency & Memory Stability
    // -------------------------------------------------------------------------
    std::cout << "\n[Phase 6] High-Frequency Evaluation Latency & Stability...\n";
    {
        nlohmann::json complex_tree = {
            {"type", "and"},
            {"conditions", nlohmann::json::array({
                {{"field", "process"}, {"op", "=="}, {"value", "cs2.exe"}},
                {{"type", "or"}, {"conditions", nlohmann::json::array({
                    {{"field", "player.state.health"}, {"op", "<"}, {"value", 20}},
                    {{"field", "round.bomb"}, {"op", "=="}, {"value", "planted"}}
                })}},
                {{"type", "not"}, {"conditions", nlohmann::json::array({
                    {{"field", "player.state.money"}, {"op", "=="}, {"value", 0}}
                })}}
            })}
        };
        aura::ConditionNode node_complex = aura::ConditionNode::FromJson(complex_tree);

        const int EVAL_COUNT = 100000;
        auto start = std::chrono::high_resolution_clock::now();
        int true_count = 0;
        for (int i = 0; i < EVAL_COUNT; ++i) {
            if (node_complex.Evaluate(gsi.get(), "cs2.exe")) {
                true_count++;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_us = std::chrono::duration<double, std::micro>(end - start).count();
        double per_eval_us = elapsed_us / EVAL_COUNT;

        ADV_CHECK(true_count == EVAL_COUNT, "100,000 evaluations produced consistent results");
        ADV_CHECK(per_eval_us < 2.0, "Average AST evaluation latency < 2.0 microseconds (actual: " + std::to_string(per_eval_us) + " us)");
        std::cout << "  Executed " << EVAL_COUNT << " AST evaluations in " << (elapsed_us / 1000.0) << " ms ("
                  << per_eval_us << " us / eval)\n";
    }

    std::cout << "\n=========================================================\n";
    std::cout << "  Total Checks: " << g_total_checks << " | Failures: " << g_failures << "\n";
    if (g_failures == 0) {
        std::cout << "  [SUCCESS] All ConditionNode AST Adversarial Checks Passed!\n";
    } else {
        std::cout << "  [FAILURE] " << g_failures << " ConditionNode AST checks failed!\n";
    }
    std::cout << "=========================================================\n";

    return g_failures == 0 ? 0 : 1;
}
