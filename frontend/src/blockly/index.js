import * as Blockly from 'blockly';
import 'blockly/blocks';
import * as En from 'blockly/msg/en';
import * as ZhHans from 'blockly/msg/zh-hans';

// Set English base locale, then overlay Simplified Chinese locale
if (typeof Blockly.setLocale === 'function') {
  Blockly.setLocale(En);
  Blockly.setLocale(ZhHans);
}

// Ensure default aria and action labels are present on Blockly.Msg
const defaultAriaMessages = {
  WORKSPACE_LABEL_MANY_STACKS: '%1 个积木块堆',
  WORKSPACE_LABEL_1_STACK: '1 个积木块堆',
  WORKSPACE_LABEL_PLAIN: '工作区',
  WORKSPACE_LABEL_MUTATOR_WORKSPACE: '突变器工作区',
  WORKSPACE_ROLEDESCRIPTION: '积木工作区',
  WORKSPACE_ARIA_LABEL: '积木工作区',
  CLEAN_UP: '整理积木',
  COLLAPSE_BLOCK: '折叠积木',
  COLLAPSE_ALL: '折叠全部',
  EXPAND_BLOCK: '展开积木',
  EXPAND_ALL: '展开全部',
  DELETE_BLOCK: '删除积木',
  DELETE_X_BLOCKS: '删除 %1 个积木',
  DELETE_ALL_BLOCKS: '删除全部 %1 个积木？'
};

if (Blockly.Msg && typeof Blockly.Msg === 'object') {
  for (const [key, val] of Object.entries(defaultAriaMessages)) {
    if (!Blockly.Msg[key]) {
      Blockly.Msg[key] = val;
    }
  }
}

export function loadSafeWorkspaceJson(jsonState, ws) {
  if (!jsonState || !ws) return;
  try {
    const state = (jsonState.blocks && !Array.isArray(jsonState.blocks))
      ? jsonState
      : { blocks: jsonState };
    Blockly.serialization.workspaces.load(state, ws);
  } catch (err) {
    console.warn('Error loading workspace JSON:', err);
  }
}

export default Blockly;
export * from 'blockly';
