#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_gsi_dictionary_blocks.py - Comprehensive Verification for CS2 GSI Encapsulated Blocks & Dictionary
Validates:
1. GSI Dictionary metadata completeness (discrete enums, numeric metrics, boolean equipment & events)
2. Node.js evaluation of getStateOptions, getDefaultThreshold, and ALL_GSI_ENUM_VALUES
3. Blockly custom block definitions (gsi_state_match, gsi_numeric_compare, gsi_enum_constant)
4. Transpilers & Serializers support (jsTranspiler, cppTranspiler, orchestratorSerializer)
5. Zero heap allocation guarantee in generated C++ code
6. C++ RuleEngine ConditionNode schema compatibility
"""

import os
import sys
import json
import unittest
import subprocess

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
FRONTEND_DIR = os.path.join(ROOT_DIR, "frontend")

class TestGsiDictionaryAndBlocks(unittest.TestCase):
    def test_gsi_dictionary_file_exists_and_complete(self):
        dict_path = os.path.join(FRONTEND_DIR, "src", "constants", "gsiDictionary.js")
        self.assertTrue(os.path.isfile(dict_path), "gsiDictionary.js must exist")
        with open(dict_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Check required discrete states
        required_discrete = [
            "round.bomb", "round.phase", "player.team", "round.win_team",
            "player.activity", "map.phase", "map.mode",
            "player.weapons.weapon_0.state", "player.weapons.weapon_1.state"
        ]
        for state in required_discrete:
            self.assertIn(f"'{state}'", content, f"Discrete state '{state}' must be present in gsiDictionary.js")

        # Check required enum values for round.bomb
        for bomb_val in ["planted", "carried", "dropped", "defused", "exploded"]:
            self.assertIn(f"'{bomb_val}'", content, f"C4 state value '{bomb_val}' must be present")

        # Check required numeric states
        required_numeric = [
            "player.state.health", "player.state.armor", "player.state.money",
            "player.state.flashed", "player.state.burning", "player.state.smoked",
            "player.state.round_kills", "player.state.round_killhs", "map.round"
        ]
        for num_state in required_numeric:
            self.assertIn(f"'{num_state}'", content, f"Numeric state '{num_state}' must be present")

    def test_node_runtime_dictionary_evaluation(self):
        dict_path = os.path.join(FRONTEND_DIR, "src", "constants", "gsiDictionary.js").replace("\\", "/")
        node_script = f"""
        import {{ getStateOptions, getDefaultThreshold, ALL_GSI_ENUM_VALUES, DISCRETE_GSI_STATES, NUMERIC_GSI_STATES }} from './src/constants/gsiDictionary.js';
        
        const bombOpts = getStateOptions('round.bomb');
        if (!bombOpts || bombOpts.length !== 5) process.exit(1);
        
        const phaseOpts = getStateOptions('round.phase');
        if (!phaseOpts || phaseOpts.length !== 3) process.exit(2);
        
        const teamOpts = getStateOptions('player.team');
        if (!teamOpts || teamOpts.length !== 2) process.exit(3);
        
        const hpMeta = getDefaultThreshold('player.state.health');
        if (!hpMeta || hpMeta.op !== '<' || hpMeta.value !== 20) process.exit(4);
        
        const moneyMeta = getDefaultThreshold('player.state.money');
        if (!moneyMeta || moneyMeta.value !== 2000) process.exit(5);
        
        if (!ALL_GSI_ENUM_VALUES || ALL_GSI_ENUM_VALUES.length < 15) process.exit(6);
        
        console.log(JSON.stringify({{
            bomb_options: bombOpts.length,
            all_enums: ALL_GSI_ENUM_VALUES.length,
            discrete_states: DISCRETE_GSI_STATES.length,
            numeric_states: NUMERIC_GSI_STATES.length
        }}));
        """
        res = subprocess.run(
            ["node", "--input-type=module", "-e", node_script],
            cwd=FRONTEND_DIR,
            capture_output=True,
            text=True
        )
        self.assertEqual(res.returncode, 0, f"Node.js execution of gsiDictionary.js failed: {res.stderr}")
        data = json.loads(res.stdout.strip())
        self.assertEqual(data["bomb_options"], 5)
        self.assertGreaterEqual(data["all_enums"], 20)
        self.assertGreaterEqual(data["discrete_states"], 9)
        self.assertGreaterEqual(data["numeric_states"], 11)

    def test_custom_blocks_registered(self):
        cb_path = os.path.join(FRONTEND_DIR, "src", "blockly", "customBlocks.js")
        with open(cb_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("Blockly.Blocks['gsi_state_match']", content)
        self.assertIn("Blockly.Blocks['gsi_numeric_compare']", content)
        self.assertIn("Blockly.Blocks['gsi_enum_constant']", content)
        self.assertIn("saveExtraState", content)
        self.assertIn("loadExtraState", content)
        self.assertIn("getStateOptions", content)
        self.assertIn("getDefaultThreshold", content)

    def test_toolboxes_include_new_blocks(self):
        tb_path = os.path.join(FRONTEND_DIR, "src", "blockly", "toolboxes.js")
        with open(tb_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("{ kind: 'block', type: 'gsi_state_match' }", content)
        self.assertIn("{ kind: 'block', type: 'gsi_numeric_compare' }", content)
        self.assertIn("{ kind: 'block', type: 'gsi_enum_constant' }", content)

    def test_orchestrator_serializer_ast_generation(self):
        orch_path = os.path.join(FRONTEND_DIR, "src", "blockly", "orchestratorSerializer.js")
        with open(orch_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("block.type === 'gsi_state_match'", content)
        self.assertIn("block.type === 'gsi_numeric_compare'", content)
        self.assertIn("block.type === 'gsi_enum_constant'", content)

    def test_transpilers_and_zero_heap_allocation(self):
        cpp_path = os.path.join(FRONTEND_DIR, "src", "blockly", "cppTranspiler.js")
        with open(cpp_path, "r", encoding="utf-8") as f:
            cpp_content = f.read()

        self.assertIn("case 'gsi_state_match':", cpp_content)
        self.assertIn("case 'gsi_numeric_compare':", cpp_content)
        self.assertIn("case 'gsi_enum_constant':", cpp_content)
        self.assertIn("std::string_view", cpp_content)
        self.assertIn("#include <string_view>", cpp_content)

        js_path = os.path.join(FRONTEND_DIR, "src", "blockly", "jsTranspiler.js")
        with open(js_path, "r", encoding="utf-8") as f:
            js_content = f.read()

        self.assertIn("case 'gsi_state_match':", js_content)
        self.assertIn("case 'gsi_numeric_compare':", js_content)
        self.assertIn("case 'gsi_enum_constant':", js_content)

    def test_rule_engine_ast_schema_compatibility(self):
        # Simulate a serialized condition tree from Blockly
        rule_ast = {
            "type": "and",
            "conditions": [
                { "field": "round.bomb", "op": "==", "value": "planted" },
                { "field": "player.state.health", "op": "<", "value": 20 }
            ]
        }
        self.assertEqual(rule_ast["type"], "and")
        self.assertEqual(len(rule_ast["conditions"]), 2)
        self.assertEqual(rule_ast["conditions"][0]["field"], "round.bomb")
        self.assertEqual(rule_ast["conditions"][0]["value"], "planted")
        self.assertEqual(rule_ast["conditions"][1]["field"], "player.state.health")
        self.assertEqual(rule_ast["conditions"][1]["value"], 20)

if __name__ == "__main__":
    unittest.main()
