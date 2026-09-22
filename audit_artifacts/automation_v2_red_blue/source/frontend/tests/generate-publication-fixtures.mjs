import fs from 'node:fs';
import path from 'node:path';
import Blockly from '../src/blockly/index.js';
import { registerCustomBlocks } from '../src/blockly/customBlocks.js';
import { CppTranspiler } from '../src/blockly/cppTranspiler.js';
registerCustomBlocks();
const number = value => ({type:'math_number',fields:{NUM:value}});
const fill = (r,g,b) => ({type:'key_fill_all',inputs:{COLOR:{block:{type:'color_rgb',inputs:{R:{block:number(r)},G:{block:number(g)},B:{block:number(b)}}}}}});
const first=fill(255,0,0),last=fill(0,0,255);
first.next={block:{type:'effect_wait_ms',inputs:{MS:{block:number(10)}},next:{block:last}}};
const workspace=new Blockly.Workspace();
Blockly.serialization.workspaces.load({blocks:{languageVersion:0,blocks:[first]}},workspace);
fs.mkdirSync(process.argv[2],{recursive:true});
for(const [name,options] of [['studio_old',undefined],['studio_continuous',{mode:'continuous'}],['studio_one_shot',{mode:'one_shot',fade_out_ms:10}]])
  fs.writeFileSync(path.join(process.argv[2],`${name}.cpp`),CppTranspiler.transpile(name,workspace,options));
workspace.dispose();
