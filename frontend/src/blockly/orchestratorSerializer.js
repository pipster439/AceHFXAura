import Blockly, { loadSafeWorkspaceJson } from './index.js';
import { canonicalConfig } from '../utils/orchestration.js';

/**
 * Serializer & Deserializer for Profile & GSI Event Orchestrator
 * Maps between Blockly AST and config.json orchestration structures with zero loss.
 */
export class OrchestratorSerializer {
  /**
   * Serializes the Orchestrator workspace into orchestration, rules, and blockly_orchestrator
   * @param {import('blockly').WorkspaceSvg} workspace 
   * @param {string} fallbackProfile 
   */
  static serializeWorkspace(workspace, fallbackProfile = 'desktop') {
    if (!workspace) {
      return {
        orchestration: { rules: [], event_overlays: [], fallback_profile: fallbackProfile },
        rules: [],
        blockly_orchestrator: { version: 1, blocks: { languageVersion: 0, blocks: [] } }
      };
    }

    // Modern Blockly v11 JSON serialization
    const blocklyJson = Blockly.serialization.workspaces.save(workspace);

    let foundFallback = fallbackProfile;
    const rules = [];
    const eventOverlays = [];

    const topBlocks = workspace.getTopBlocks(true);
    for (const block of topBlocks) {
      if (block.type === 'orch_root_flow') {
        const fb = block.getFieldValue('FALLBACK_PROFILE');
        if (fb) foundFallback = fb.trim();

        const doBlock = block.getInputTargetBlock('DO');
        if (doBlock) {
          this.processStatementChain(doBlock, null, rules, eventOverlays);
        }
      } else if (block.type === 'orchestrator_root') {
        const fb = block.getFieldValue('FALLBACK_PROFILE');
        if (fb) foundFallback = fb.trim();

        // Traverse overlays
        let currOverlay = block.getInputTargetBlock('OVERLAYS');
        while (currOverlay) {
          if (currOverlay.type === 'event_overlay') {
            eventOverlays.push({
              event: currOverlay.getFieldValue('EVENT') || 'event.kill',
              effect: currOverlay.getFieldValue('EFFECT') || 'kill_pulse',
              duration_ms: Number(currOverlay.getFieldValue('DURATION')) || 1200,
              fade_ms: Number(currOverlay.getFieldValue('FADE')) || 400,
              priority: Number(currOverlay.getFieldValue('PRIORITY')) || 10
            });
          }
          currOverlay = currOverlay.getNextBlock();
        }

        // Traverse process rules
        let currRule = block.getInputTargetBlock('RULES');
        let ruleIdx = 0;
        while (currRule) {
          if (currRule.type === 'match_process') {
            const proc = (currRule.getFieldValue('PROCESS') || '').trim();
            const targetProfile = (currRule.getFieldValue('TARGET_PROFILE') || '').trim();
            const dnd = currRule.getFieldValue('DND') === 'TRUE';
            const condBlock = currRule.getInputTargetBlock('CONDITION');
            const conditionAst = this.parseConditionAst(condBlock);

            rules.push({
              id: `rule_${proc.replace(/[^a-zA-Z0-9_]/g, '')}_${ruleIdx++}`,
              source: 'process', enabled: !!proc,
              process: proc,
              dnd: dnd,
              condition: conditionAst,
              target_profile: targetProfile
            });
          }
          currRule = currRule.getNextBlock();
        }
      }
    }


    return {
      orchestration: {
        version: 2,
        rules,
        event_overlays: eventOverlays,
        fallback_profile: foundFallback
      },
      rules: [],
      gsi_bindings: [],
      blockly_orchestrator: {
        version: 1,
        blockly_json: blocklyJson
      }
    };
  }

  static processStatementChain(startBlock, inheritedCond, rules, eventOverlays) {
    let curr = startBlock;

    while (curr) {
      if (curr.isEnabled?.() === false) { curr = curr.getNextBlock(); continue; }
      if (curr.type === 'orch_action_wait_ms') throw new Error('联动按条件持续评估；请把等待和顺序播放放进所调用的光效里');
      if (curr.type === 'controls_if') {
        let i = 0;
        let cumulativeElse = [];
        while (curr.getInput(`IF${i}`)) {
          const branchTarget = curr.getInputTargetBlock(`IF${i}`);
          const rawCond = this.parseConditionAst(branchTarget);
          
          let branchCond = cumulativeElse.length
            ? { type: 'and', conditions: [rawCond, { type: 'not', conditions: [{ type: 'or', conditions: [...cumulativeElse] }] }] }
            : rawCond;
          if (inheritedCond && Object.keys(inheritedCond).length > 0) {
            branchCond = { type: 'and', conditions: [inheritedCond, branchCond] };
          }
          
          const doBlock = curr.getInputTargetBlock(`DO${i}`);
          if (doBlock) {
            this.processStatementChain(doBlock, branchCond, rules, eventOverlays);
          }
          if (Object.keys(rawCond).length > 0) {
            cumulativeElse.push(rawCond);
          }
          i++;
        }

        const elseBlock = curr.getInputTargetBlock('ELSE');
        if (elseBlock && cumulativeElse.length > 0) {
          const notCond = { 
            type: 'not', 
            conditions: cumulativeElse.length === 1 ? [cumulativeElse[0]] : [{ type: 'or', conditions: cumulativeElse }] 
          };
          let elseCond = notCond;
          if (inheritedCond && Object.keys(inheritedCond).length > 0) {
            elseCond = { type: 'and', conditions: [inheritedCond, notCond] };
          }
          this.processStatementChain(elseBlock, elseCond, rules, eventOverlays);
        }
      } else if (curr.type === 'orch_action_switch_profile') {
        const targetProfile = (curr.getFieldValue('PROFILE') || 'default').trim();
        const proc = ''; // Keep OR/NOT process predicates entirely in the condition AST.
        rules.push({
          id: `flow_rule_${rules.length}_${proc.replace(/[^a-zA-Z0-9_]/g, '')}`,
          process: proc,
          dnd: false,
          condition: inheritedCond || {},
          target_profile: targetProfile
        });
      } else if (curr.type === 'orch_action_overlay_pulse' || curr.type === 'orch_action_overlay_state') {
        eventOverlays.push({
          id: `overlay_${eventOverlays.length}`,
          trigger: curr.type === 'orch_action_overlay_state' ? 'state' : 'event',
          condition: inheritedCond || {},
          blend_mode: curr.getFieldValue('BLEND') || 'blend',
          event: curr.getFieldValue('EVENT') || 'event.kill',
          effect: (curr.getFieldValue('EFFECT') || 'rainbow_wave').trim(),
          duration_ms: Number(curr.getFieldValue('DURATION')) || 1200,
          fade_ms: Number(curr.getFieldValue('FADE') ?? 400),
          priority: Number(curr.getFieldValue('PRIORITY')) || 20
        });
      } else if (curr.type === 'orch_action_set_dnd') {
        const isDnd = curr.getFieldValue('DND') === 'TRUE';
        if (!rules.length || JSON.stringify(rules[rules.length - 1].condition) !== JSON.stringify(inheritedCond || {})) throw new Error('免打扰设置请紧跟同一条件下的切换方案积木');
        rules[rules.length - 1].dnd = isDnd;
      } else if (curr.type === 'match_process') {
        const proc = (curr.getFieldValue('PROCESS') || '').trim();
        const targetProfile = (curr.getFieldValue('TARGET_PROFILE') || '').trim();
        const dnd = curr.getFieldValue('DND') === 'TRUE';
        const condBlock = curr.getInputTargetBlock('CONDITION');
        const conditionAst = this.parseConditionAst(condBlock);

        rules.push({
          id: `rule_${proc.replace(/[^a-zA-Z0-9_]/g, '')}_${rules.length}`,
          source: 'process', enabled: !!proc,
          process: proc,
          dnd: dnd,
          condition: inheritedCond ? { type: 'and', conditions: [inheritedCond, conditionAst] } : conditionAst,
          target_profile: targetProfile
        });
      } else if (curr.type === 'event_overlay') {
        eventOverlays.push({
          id: `overlay_${eventOverlays.length}`,
          trigger: curr.type === 'orch_action_overlay_state' ? 'state' : 'event',
          condition: inheritedCond || {},
          blend_mode: curr.getFieldValue('BLEND') || 'blend',
          event: curr.getFieldValue('EVENT') || 'event.kill',
          effect: curr.getFieldValue('EFFECT') || 'kill_pulse',
          duration_ms: Number(curr.getFieldValue('DURATION')) || 1200,
          fade_ms: Number(curr.getFieldValue('FADE') ?? 400),
          priority: Number(curr.getFieldValue('PRIORITY')) || 10
        });
      }

      curr = curr.getNextBlock();
    }
  }

  static extractProcessFromCondition(cond) {
    if (!cond) return null;
    if (cond.field && (cond.field.toLowerCase() === 'process.name' || cond.field.toLowerCase() === 'process')) {
      return String(cond.value || '');
    }
    if (cond.conditions && Array.isArray(cond.conditions)) {
      for (const c of cond.conditions) {
        const found = this.extractProcessFromCondition(c);
        if (found) return found;
      }
    }
    return null;
  }

  static extractBlockValue(block, fallback) {
    if (!block) return fallback;
    if (block.type === 'gsi_enum_constant') {
      return block.getFieldValue('VALUE') ?? fallback;
    }
    if (block.type === 'math_number') {
      return Number(block.getFieldValue('NUM')) || 0;
    }
    if (block.type === 'text') {
      return block.getFieldValue('TEXT') ?? '';
    }
    if (block.type === 'logic_boolean') {
      return block.getFieldValue('BOOL') === 'TRUE';
    }
    return fallback;
  }

  static parseConditionAst(block) {
    if (!block) return {};
    if (block.type === 'logic_boolean') return { type: block.getFieldValue('BOOL') === 'TRUE' ? 'and' : 'or', conditions: [] };

    if (block.type === 'gsi_state_match') {
      const field = block.getFieldValue('STATE') || 'round.bomb';
      const op = block.getFieldValue('OP') || '==';
      const value = block.getFieldValue('VALUE') || '';
      return { field, op, value };
    }

    if (block.type === 'gsi_numeric_compare') {
      const field = block.getFieldValue('FIELD') || 'player.state.health';
      const op = block.getFieldValue('OP') || '<';
      const rawVal = block.getFieldValue('VALUE');
      const value = (rawVal !== null && rawVal !== undefined && !isNaN(Number(rawVal))) ? Number(rawVal) : 0;
      return { field, op, value };
    }

    if (block.type === 'condition_and' || (block.type === 'logic_operation' && block.getFieldValue('OP') === 'AND')) {
      const c0 = this.parseConditionAst(block.getInputTargetBlock('COND0') || block.getInputTargetBlock('A'));
      const c1 = this.parseConditionAst(block.getInputTargetBlock('COND1') || block.getInputTargetBlock('B'));
      const sub = [c0, c1].filter((c) => Object.keys(c).length > 0);
      return { type: 'and', conditions: sub };
    }

    if (block.type === 'condition_or' || (block.type === 'logic_operation' && block.getFieldValue('OP') === 'OR')) {
      const c0 = this.parseConditionAst(block.getInputTargetBlock('COND0') || block.getInputTargetBlock('A'));
      const c1 = this.parseConditionAst(block.getInputTargetBlock('COND1') || block.getInputTargetBlock('B'));
      const sub = [c0, c1].filter((c) => Object.keys(c).length > 0);
      return { type: 'or', conditions: sub };
    }

    if (block.type === 'condition_not' || block.type === 'logic_negate') {
      const c = this.parseConditionAst(block.getInputTargetBlock('COND') || block.getInputTargetBlock('BOOL'));
      return { type: 'not', conditions: Object.keys(c).length > 0 ? [c] : [] };
    }

    if (block.type === 'orch_event_triggered') {
      const ev = block.getFieldValue('EVENT') || 'event.kill';
      return { field: ev, op: '==', value: true };
    }

    if (block.type === 'orch_text_equals') {
      const targetA = block.getInputTargetBlock('A');
      const targetB = block.getInputTargetBlock('B');
      let field = 'process.name';
      let val = 'cs2.exe';
      if (targetA?.type === 'orch_current_process') {
        field = 'process.name';
        val = this.extractBlockValue(targetB, 'cs2.exe');
      } else if (targetA?.type === 'orch_gsi_str' || targetA?.type === 'gsi_get_string') {
        field = targetA.getFieldValue('PATH') || 'round.bomb';
        val = this.extractBlockValue(targetB, '');
      } else if (targetB?.type === 'orch_current_process') {
        field = 'process.name';
        val = this.extractBlockValue(targetA, 'cs2.exe');
      } else if (targetB?.type === 'orch_gsi_str' || targetB?.type === 'gsi_get_string') {
        field = targetB.getFieldValue('PATH') || 'round.bomb';
        val = this.extractBlockValue(targetA, '');
      } else {
        val = this.extractBlockValue(targetB, '');
      }
      return { field, op: '==', value: val };
    }

    if (block.type === 'logic_compare') {
      const opMap = { EQ: '==', NEQ: '!=', LT: '<', LTE: '<=', GT: '>', GTE: '>=' };
      const opKey = block.getFieldValue('OP') || 'EQ';
      let op = opMap[opKey] || '==';
      const targetA = block.getInputTargetBlock('A');
      const targetB = block.getInputTargetBlock('B');

      let field = 'player.state.health';
      let val = 0;

      if (targetA?.type === 'orch_current_process') {
        field = 'process.name';
        val = this.extractBlockValue(targetB, 'cs2.exe');
      } else if (
        targetA?.type === 'orch_gsi_num' || targetA?.type === 'orch_gsi_str' || targetA?.type === 'orch_gsi_bool' ||
        targetA?.type === 'gsi_get_number' || targetA?.type === 'gsi_get_string' || targetA?.type === 'gsi_get_boolean'
      ) {
        field = targetA.getFieldValue('PATH') || 'player.state.health';
        val = this.extractBlockValue(targetB, 0);
      } else if (
        targetB?.type === 'orch_gsi_num' || targetB?.type === 'orch_gsi_str' ||
        targetB?.type === 'gsi_get_number' || targetB?.type === 'gsi_get_string'
      ) {
        field = targetB.getFieldValue('PATH') || 'player.state.health';
        val = this.extractBlockValue(targetA, 0);
        op = ({ '<': '>', '<=': '>=', '>': '<', '>=': '<=' })[op] || op;
      } else {
        throw new Error('联动比较需要一个 GSI 字段和一个常量；复杂运算请放在光效内');
      }
      return { field, op, value: val };
    }

    if (block.type === 'condition_compare') {
      const field = block.getFieldValue('FIELD') || 'player.state.health';
      const op = block.getFieldValue('OP') || '==';
      const rawVal = block.getFieldValue('VALUE') || '0';
      let val = rawVal;
      if (!isNaN(Number(rawVal)) && rawVal.trim() !== '') {
        val = Number(rawVal);
      } else if (rawVal.toLowerCase() === 'true') {
        val = true;
      } else if (rawVal.toLowerCase() === 'false') {
        val = false;
      }
      return { field, op, value: val };
    }

    throw new Error(`不支持的联动条件：${block.type}`);
  }

  /**
   * Scans the workspace and collects all effect and profile names referenced
   */
  static getUsedEffects(workspace) {
    if (!workspace) return [];
    const used = new Set();
    const allBlocks = workspace.getAllBlocks(false);
    for (const b of allBlocks) {
      if (b.type === 'orch_action_switch_profile') {
        const p = b.getFieldValue('PROFILE');
        if (p) used.add(p.trim());
      } else if (b.type === 'orch_action_overlay_pulse' || b.type === 'orch_action_overlay_state') {
        const ef = b.getFieldValue('EFFECT');
        if (ef) used.add(ef.trim());
      } else if (b.type === 'match_process') {
        const p = b.getFieldValue('TARGET_PROFILE');
        if (p) used.add(p.trim());
      } else if (b.type === 'event_overlay') {
        const ef = b.getFieldValue('EFFECT');
        if (ef) used.add(ef.trim());
      } else if (b.type === 'orch_root_flow' || b.type === 'orchestrator_root') {
        const fb = b.getFieldValue('FALLBACK_PROFILE');
        if (fb) used.add(fb.trim());
      }
    }
    return Array.from(used);
  }

  /**
   * Returns list of effects used in orchestrator that are custom studio effects but lack compiled DLLs
   */
  static getUncompiledEffects(workspace, config, compiledPluginNames = []) {
    const used = this.getUsedEffects(workspace);
    const customStudioEffects = Object.keys(config?.blockly_effects || {});
    const compiledSet = new Set(compiledPluginNames.map(n => n.toLowerCase()));

    return used.filter(effectName => {
      // Must be a custom blockly studio effect
      if (!customStudioEffects.includes(effectName)) return false;
      // If it's not compiled in compiledSet
      return !compiledSet.has(effectName.toLowerCase());
    });
  }

  /**
   * Restores workspace from blockly_orchestrator state or migrates legacy rules
   */
  static restoreWorkspace(workspace, config) {
    if (!workspace) return;
    workspace.clear();

    if (config?.blockly_orchestrator?.blockly_json) {
      loadSafeWorkspaceJson(config.blockly_orchestrator.blockly_json, workspace);
      return;
    }

    const canonical = canonicalConfig(config);
    const statements = [];
    const conditionBlock = (ast) => {
      if (!ast || Object.keys(ast).length === 0) return { type: 'logic_boolean', fields: { BOOL: 'TRUE' } };
      if (ast.type === 'not') return { type: 'logic_negate', inputs: { BOOL: { block: conditionBlock(ast.conditions?.[0]) } } };
      if (ast.type === 'and' || ast.type === 'or') {
        const children = ast.conditions || [];
        if (!children.length) return { type: 'logic_boolean', fields: { BOOL: ast.type === 'and' ? 'TRUE' : 'FALSE' } };
        return children.slice(1).reduce((left, right) => ({ type: 'logic_operation', fields: { OP: ast.type.toUpperCase() }, inputs: { A: { block: left }, B: { block: conditionBlock(right) } } }), conditionBlock(children[0]));
      }
      return { type: 'condition_compare', fields: { FIELD: ast.field, OP: ast.op || '==', VALUE: String(ast.value ?? '') } };
    };
    for (const r of canonical.orchestration.rules) {
      statements.push({ type: 'match_process', fields: { PROCESS: r.process || '', TARGET_PROFILE: r.target_profile, DND: r.dnd ? 'TRUE' : 'FALSE' }, inputs: Object.keys(r.condition || {}).length ? { CONDITION: { block: conditionBlock(r.condition) } } : {} });
    }
    for (const ov of canonical.orchestration.event_overlays) {
      const action = { type: ov.trigger === 'state' ? 'orch_action_overlay_state' : 'orch_action_overlay_pulse', fields: { EFFECT: ov.effect, PRIORITY: ov.priority ?? 10, BLEND: ov.blend_mode || 'blend', ...(ov.trigger === 'state' ? {} : { EVENT: ov.event || 'event.kill', DURATION: ov.duration_ms ?? 1200, FADE: ov.fade_ms ?? ov.fade_out_ms ?? 400 }) } };
      statements.push({ type: 'controls_if', inputs: { IF0: { block: conditionBlock(ov.condition) }, DO0: { block: action } } });
    }
    for (let i = statements.length - 2; i >= 0; --i) statements[i].next = { block: statements[i + 1] };
    loadSafeWorkspaceJson({ blocks: { languageVersion: 0, blocks: [{ type: 'orch_root_flow', x: 40, y: 40, fields: { FALLBACK_PROFILE: canonical.orchestration.fallback_profile }, inputs: statements.length ? { DO: { block: statements[0] } } : {} }] } }, workspace);
  }
}
