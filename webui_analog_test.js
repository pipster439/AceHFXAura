/**
 * 前端「模拟触发」开关状态机验证（jsdom）。
 *
 * 背景：修复问题 5 时，我最初的实现会在切离常亮时强制清空 isAnalogEnabled，
 * 导致「常亮(开模拟) -> 涟漪 -> 切回常亮」后用户偏好丢失。改为区分
 * 「偏好记忆(isAnalogEnabled)」与「当前生效(analogActive())」后，需要实测确认
 * 往返切换真的能恢复，而不是只靠读代码下结论。
 *
 * 页面无 canvas（键盘预览为纯 DOM），因此 jsdom 足以驱动真实的前端脚本。
 */
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const ROOT = __dirname;
const HTML = fs.readFileSync(path.join(ROOT, 'web', 'index.html'), 'utf8');
const CONFIG = JSON.parse(fs.readFileSync(path.join(ROOT, 'e2e_webui_config.json'), 'utf8'));

const results = [];
function check(name, cond, detail) {
    results.push({ name, ok: !!cond, detail: detail || '' });
    console.log(`  [${cond ? 'PASS' : 'FAIL'}] ${name}${detail ? ' -- ' + detail : ''}`);
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function main() {
    console.log('='.repeat(76));
    console.log('前端模拟触发开关 —— 状态机往返验证 (jsdom)');
    console.log('='.repeat(76));

    const dom = new JSDOM(HTML, {
        runScripts: 'dangerously',
        url: 'http://127.0.0.1:19899/',
        pretendToBeVisual: false,
        beforeParse(window) {
            // 渲染循环无需执行，停掉 rAF，避免 jsdom 空转
            window.requestAnimationFrame = () => 0;
            window.cancelAnimationFrame = () => { };

            // fetch 桩：指向本地测试配置，不触碰真实 config.json
            // 同时记录最后一次 POST 的请求体，用于验证「拨开关 -> 保存」真正落盘的值
            window.__lastPostBody = null;
            window.fetch = async (url, opts) => {
                if (String(url).includes('/api/config')) {
                    if (opts && opts.method === 'POST') {
                        window.__lastPostBody = opts.body;
                        return { ok: true, status: 200, json: async () => ({ message: 'ok' }) };
                    }
                    return { ok: true, status: 200, json: async () => JSON.parse(JSON.stringify(CONFIG)) };
                }
                if (String(url).includes('/api/status')) {
                    return { ok: true, status: 200, json: async () => ({ daemon_running: false }) };
                }
                return { ok: false, status: 404, json: async () => ({}) };
            };
            window.HTMLElement.prototype.scrollIntoView = function () { };
        },
    });

    const win = dom.window;
    // 捕获前端脚本错误
    const scriptErrors = [];
    win.addEventListener('error', e => scriptErrors.push(String(e.message || e.error)));

    // 等待 DOMContentLoaded 与异步 loadConfig 完成
    await sleep(600);

    const ev = (expr) => win.eval(expr);
    const txt = (sel) => {
        const el = win.document.querySelector(sel);
        return el ? (el.textContent || '').trim() : null;
    };
    const display = (sel) => {
        const el = win.document.querySelector(sel);
        return el ? el.style.display : null;
    };

    if (scriptErrors.length) {
        console.log('  [警告] 前端脚本报错:', scriptErrors.join(' | '));
    }

    // --- 0. 初始加载 ---
    const loadedProfile = ev('currentProfileName');
    check('配置加载成功', !!ev('currentConfig'), `当前方案=${loadedProfile}`);

    // --- 1. 切到常亮，模拟开关应可见 ---
    win.selectEffect('static');
    check('常亮: 模拟开关可见', display('#param-group-analog') === 'flex',
        `display=${display('#param-group-analog')}`);

    // --- 2. 打开模拟触发 ---
    win.toggleAnalogSwitch();
    check('点击后开关显示 ON', txt('#analog-switch .cyber-switch-text') === 'ON',
        `文本=${txt('#analog-switch .cyber-switch-text')}`);
    check('isAnalogEnabled 为 true', ev('isAnalogEnabled') === true);
    check('analogActive() 为 true', ev('analogActive()') === true);

    // 开关本身只改内存态；配置对象由 saveCurrentInputsToProfile() 同步
    //（saveConfiguration() 在 POST 前会调用它，所以「拨开关 -> 保存」不会丢）
    win.saveCurrentInputsToProfile();
    check('同步后 prof.analog 写为 true',
        ev('currentConfig.profiles[currentProfileName].analog') === true);

    // 完整保存路径：拨开关 -> 点保存 -> 校验真正发往服务端的 JSON
    const postedAnalog = await (async () => {
        await win.saveConfiguration();
        const body = win.__lastPostBody ? JSON.parse(win.__lastPostBody) : null;
        if (!body) return null;
        const pn = body.default_profile || 'desktop';
        return body.profiles?.[pn]?.analog;
    })();
    check('常亮+模拟ON: 保存出去的 JSON 中 analog=true', postedAnalog === true,
        `posted analog=${postedAnalog}`);

    // --- 3. 切到涟漪（键盘响应型），模拟触发必须失效 ---
    win.selectEffect('ripple');
    check('涟漪: 模拟开关隐藏', display('#param-group-analog') === 'none',
        `display=${display('#param-group-analog')}`);
    check('涟漪: analogActive() 为 false', ev('analogActive()') === false);
    check('涟漪: 配置中 prof.analog 写为 false',
        ev('currentConfig.profiles[currentProfileName].analog') === false);

    const postedRipple = await (async () => {
        win.__lastPostBody = null;
        await win.saveConfiguration();
        const body = win.__lastPostBody ? JSON.parse(win.__lastPostBody) : null;
        if (!body) return null;
        const pn = body.default_profile || 'desktop';
        return body.profiles?.[pn]?.analog;
    })();
    check('涟漪: 保存出去的 JSON 中 analog=false（键盘响应型不含模拟触发）',
        postedRipple === false, `posted analog=${postedRipple}`);

    // --- 4. 切回常亮：关键断言 —— 用户偏好应当自动恢复 ---
    win.selectEffect('static');
    check('切回常亮: 模拟开关重新可见', display('#param-group-analog') === 'flex',
        `display=${display('#param-group-analog')}`);
    check('切回常亮: 开关仍显示 ON（偏好已恢复）',
        txt('#analog-switch .cyber-switch-text') === 'ON',
        `文本=${txt('#analog-switch .cyber-switch-text')}`);
    check('切回常亮: analogActive() 恢复为 true', ev('analogActive()') === true);
    check('切回常亮: prof.analog 恢复为 true',
        ev('currentConfig.profiles[currentProfileName].analog') === true);

    // --- 5. reactive 同样应隐藏模拟触发 ---
    win.selectEffect('reactive');
    check('按键响应: 模拟开关隐藏', display('#param-group-analog') === 'none');
    check('按键响应: analogActive() 为 false', ev('analogActive()') === false);

    // --- 6. 星空/流沙/电流/雨滴 均不应提供模拟触发 ---
    for (const eff of ['starry_night', 'quicksand', 'current', 'raindrop', 'wave', 'breathing', 'color_cycle']) {
        win.selectEffect(eff);
        const hidden = display('#param-group-analog') === 'none';
        const inactive = ev('analogActive()') === false;
        check(`${eff}: 无模拟触发`, hidden && inactive,
            `display=${display('#param-group-analog')} active=${ev('analogActive()')}`);
    }

    // --- 7. 端到端往返：常亮(ON) -> 涟漪 -> 按键响应 -> 常亮 ---
    win.selectEffect('static');
    if (ev('isAnalogEnabled') !== true) win.toggleAnalogSwitch();
    win.selectEffect('ripple');
    win.selectEffect('reactive');
    win.selectEffect('starry_night');
    win.selectEffect('static');
    check('多次往返后偏好仍为 ON',
        txt('#analog-switch .cyber-switch-text') === 'ON' && ev('analogActive()') === true,
        `文本=${txt('#analog-switch .cyber-switch-text')} active=${ev('analogActive()')}`);

    // --- 8. 用户主动关闭后，往返不应复活 ---
    win.toggleAnalogSwitch(); // 关闭
    win.selectEffect('ripple');
    win.selectEffect('static');
    check('用户关闭后切回仍为 OFF',
        txt('#analog-switch .cyber-switch-text') === 'OFF' && ev('analogActive()') === false,
        `文本=${txt('#analog-switch .cyber-switch-text')}`);

    // --- 9. 确认无自动演练残留 ---
    const hasDemo = /demoList|demoStepCounter/.test(HTML);
    check('页面无自动演练代码残留', !hasDemo);
    check('analogActive() 函数存在', typeof win.analogActive === 'function');

    win.close();

    console.log('='.repeat(76));
    const failed = results.filter(r => !r.ok);
    console.log(`合计 ${results.length} 项，通过 ${results.length - failed.length} 项，失败 ${failed.length} 项`);
    failed.forEach(f => console.log(`  - ${f.name}: ${f.detail}`));
    console.log('='.repeat(76));
    return failed.length ? 1 : 0;
}

main().then(code => process.exit(code)).catch(e => {
    console.error('测试异常:', e);
    process.exit(2);
});
