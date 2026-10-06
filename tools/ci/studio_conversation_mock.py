"""Local deterministic conversation fixture. Never used for real providers."""
import json
import time

def reply_for(context, requests, rejection_case='publish'):
    studio = context['current_project']['studio']
    prompt = next(m['text'] for m in reversed(context['conversation']) if m['role'] == 'user')
    rounds = context['tool_rounds']
    requests.append({'prompt': prompt, 'round': len(rounds), 'capabilities': studio['capabilities'],
                     'preset_ids': [p['id'] for p in studio['presets']], 'node_count': len(studio['nodes']),
                     'history_count': len(context['conversation']), 'fingerprint': context['current_project']['fingerprint'],
                     'tools': [r['tool'] for r in rounds], 'diagnostics': [r['result'].get('diagnostic', '') for r in rounds if r['tool'] in ('get_build_errors', 'get_validation_errors')]})
    if prompt == 'cancel-wait': time.sleep(4)
    node = next((n for n in studio['nodes'] if n['input'] == 'PERIOD_SEC'), None)
    def tool(name, arguments=None): return {'message': '我先检查当前工程。', 'tool_calls': [{'name': name, 'arguments': arguments or {}}]}
    def text(message): return {'message': message, 'tool_calls': []}
    if prompt == '为什么验证失败？': return '实际诊断需查看工程验证结果；目前草稿验证通过。普通文字说明不会修改工程。'
    if prompt == '你能修吗？':
        if rounds: return '已准备可验证的周期建议；请查看 Diff，自行决定是否应用。'
        return tool('propose_effect_change', {'action': 'propose', 'summary': '提供可验证的周期建议，不声称修复不存在的错误。', 'preset': None, 'edits': [{'node_id': node['node_id'], 'value': 2}]})
    if prompt == 'malformed-with-text':
        if not rounds: return tool('propose_effect_change', {'action': 'propose', 'summary': '先验证候选，最终回复仍需通过动作校验。', 'preset': None, 'edits': [{'node_id': node['node_id'], 'value': 2.2}]})
        return {'message': '我建议改这里；这条动作无效。', 'tool_calls': [{'name': 'propose_effect_change', 'arguments': {'action': 'apply', 'path': 'C:/fixture/private'}}]}
    if prompt == 'invalid-action':
        if rejection_case == 'malformed': return tool('propose_effect_change', {'action': 'apply'})
        return tool('unknown_tool' if rejection_case == 'unknown' else 'publish')
    if prompt == 'repeat-tools': return tool('get_capabilities')
    if rounds:
        last = rounds[-1]
        if last['tool'] == 'get_build_errors':
            diagnostic = last['result']['diagnostic']
            if 'C2039' not in diagnostic: return text('没有相关构建错误。')
            if '能修' in prompt and node:
                return tool('propose_effect_change', {'action': 'propose', 'summary': 'C2039 是受限 API 错误；准备一个可验证的周期调整候选，无法自动修改原生成员。', 'preset': None, 'edits': [{'node_id': node['node_id'], 'value': 2}]})
            return text('C2039 表示成员不存在；当前可用工具无法安全自动修改这一部分。')
        if last['tool'] == 'get_active_proposal':
            return text('刚才的候选验证结果：' + last['result'].get('diagnostic', '验证通过'))
        if last['tool'] == 'propose_effect_change':
            proposal = last['result']
            if not proposal['valid']: return text('候选未通过验证：' + proposal['diagnostic'])
            return text(('已检查 C2039，原生成员错误超出安全工具范围。' if any(r['tool'] == 'get_build_errors' for r in rounds) else '') + '保持颜色，周期 ' + proposal['changes'][0]['before'] + ' → ' + proposal['changes'][0]['after'] + ' 秒；请查看 Diff 后决定应用。')
    if '失败' in prompt or '编译' in prompt: return tool('get_build_errors')
    if '没通过验证' in prompt: return tool('get_active_proposal')
    if '为什么' in prompt or '解释' in prompt: return text('当前效果使用周期变化；周期较短会让变化更快。衰减积木也会影响消失速度。')
    if '改了什么' in prompt or '具体变了什么' in prompt or '相比' in prompt:
        active = context['current_project']['active_proposal']
        return text('当前草稿周期为 ' + str(node['value']) + ' 秒；最近建议：' + json.dumps(active.get('changes', []) if active else [], ensure_ascii=False) + '，颜色未变。')
    if '颜色别动' == prompt: return text('会保留当前颜色。')
    if node:
        value = 2.5 if '再慢' in prompt or '太快' in prompt else 2
        return tool('propose_effect_change', {'action': 'propose', 'summary': '保留颜色，调整当前周期。', 'preset': None, 'edits': [{'node_id': node['node_id'], 'value': value}]})
    return text('当前可用工具无法安全自动修改这一部分。')
