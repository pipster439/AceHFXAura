import * as Blockly from 'blockly/core';

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
      if (block.type === 'orchestrator_root') {
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

    // Synthesize backward-compatible rules array for older daemon engines
    const legacyRules = rules.map((r) => ({
      process: r.process,
      profile: r.target_profile,
      suppress_web_ui: r.dnd
    }));

    return {
      orchestration: {
        rules,
        event_overlays: eventOverlays,
        fallback_profile: foundFallback
      },
      rules: legacyRules,
      blockly_orchestrator: {
        version: 1,
        blockly_json: blocklyJson
      }
    };
  }

  static parseConditionAst(block) {
    if (!block) return {};

    if (block.type === 'condition_and') {
      const c0 = this.parseConditionAst(block.getInputTargetBlock('COND0'));
      const c1 = this.parseConditionAst(block.getInputTargetBlock('COND1'));
      const sub = [c0, c1].filter((c) => Object.keys(c).length > 0);
      return { type: 'and', conditions: sub };
    }

    if (block.type === 'condition_or') {
      const c0 = this.parseConditionAst(block.getInputTargetBlock('COND0'));
      const c1 = this.parseConditionAst(block.getInputTargetBlock('COND1'));
      const sub = [c0, c1].filter((c) => Object.keys(c).length > 0);
      return { type: 'or', conditions: sub };
    }

    if (block.type === 'condition_not') {
      const c = this.parseConditionAst(block.getInputTargetBlock('COND'));
      return { type: 'not', conditions: Object.keys(c).length > 0 ? [c] : [] };
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

    return {};
  }

  /**
   * Restores workspace from blockly_orchestrator state or migrates legacy rules
   */
  static restoreWorkspace(workspace, config) {
    if (!workspace) return;
    workspace.clear();

    if (config?.blockly_orchestrator?.blockly_json) {
      try {
        Blockly.serialization.workspaces.load(config.blockly_orchestrator.blockly_json, workspace);
        return;
      } catch (err) {
        console.warn('Error loading blockly_orchestrator, building default workspace:', err);
      }
    }

    // Auto-create default root block with legacy rules
    const rootBlock = workspace.newBlock('orchestrator_root');
    rootBlock.initSvg();
    rootBlock.render();
    rootBlock.setFieldValue(config?.orchestration?.fallback_profile || config?.default_profile || 'desktop', 'FALLBACK_PROFILE');
    rootBlock.moveBy(50, 50);

    const rules = config?.orchestration?.rules || (config?.rules || []).map((r, idx) => ({
      id: `rule_${idx}`,
      process: r.process,
      target_profile: r.profile,
      dnd: r.suppress_web_ui !== false
    }));

    let prevRule = null;
    rules.forEach((r, idx) => {
      const ruleBlock = workspace.newBlock('match_process');
      ruleBlock.initSvg();
      ruleBlock.render();
      ruleBlock.setFieldValue(r.process || 'cs2.exe', 'PROCESS');
      ruleBlock.setFieldValue(r.target_profile || r.profile || 'cs2_gamer', 'TARGET_PROFILE');
      ruleBlock.setFieldValue(r.dnd ? 'TRUE' : 'FALSE', 'DND');

      if (idx === 0) {
        rootBlock.getInput('RULES').connection.connect(ruleBlock.previousConnection);
      } else if (prevRule) {
        prevRule.nextConnection.connect(ruleBlock.previousConnection);
      }
      prevRule = ruleBlock;
    });

    const overlays = config?.orchestration?.event_overlays || [];
    let prevOv = null;
    overlays.forEach((ov, idx) => {
      const ovBlock = workspace.newBlock('event_overlay');
      ovBlock.initSvg();
      ovBlock.render();
      ovBlock.setFieldValue(ov.event || 'event.kill', 'EVENT');
      ovBlock.setFieldValue(ov.effect || 'kill_pulse', 'EFFECT');
      ovBlock.setFieldValue(ov.duration_ms || 1200, 'DURATION');
      ovBlock.setFieldValue(ov.fade_ms || 400, 'FADE');
      ovBlock.setFieldValue(ov.priority || 10, 'PRIORITY');

      if (idx === 0) {
        rootBlock.getInput('OVERLAYS').connection.connect(ovBlock.previousConnection);
      } else if (prevOv) {
        prevOv.nextConnection.connect(ovBlock.previousConnection);
      }
      prevOv = ovBlock;
    });
  }
}
