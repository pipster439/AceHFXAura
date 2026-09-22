"""DEVELOPMENT / PRE-RELEASE only. Explicit, all-or-nothing Automation migration."""
import argparse
import copy
import difflib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


class MigrationError(ValueError):
    pass


def fields(value, allowed, path):
    if not isinstance(value, dict):
        raise MigrationError(f'{path}: expected object')
    for a,b in (('op','operator'),('effect','profile'),('target_profile','profile'),('dnd','suppress_web_ui'),('fade_ms','fade_out_ms')):
        if a in value and b in value and value[a] != value[b]:
            raise MigrationError(f'{path}: conflicting aliases {a}/{b}')
    unknown = set(value) - set(allowed)
    if unknown:
        raise MigrationError(f'{path}: unmapped fields: {sorted(unknown)}')


def process(name, path):
    if not isinstance(name, str) or not name or name.lower() != name or not name.endswith('.exe') or any(c in name for c in '/\\ \t\n'):
        raise MigrationError(f'{path}: process spelling cannot be mapped losslessly; require lowercase filename.exe')
    return {'field': 'process.name', 'op': '==', 'value': name}


def condition(value, path):
    fields(value, ('field', 'op', 'operator', 'value', 'type', 'conditions', 'and', 'or'), path)
    if 'and' in value or 'or' in value or 'conditions' in value:
        op = 'and' if 'and' in value else 'or' if 'or' in value else value.get('type')
        children = value.get(op, value.get('conditions'))
        if op not in ('and', 'or') or not isinstance(children, list) or not children:
            raise MigrationError(f'{path}: unsupported logical condition')
        return {op: [condition(c, f'{path}/{i}') for i, c in enumerate(children)]}
    key, op, target = value.get('field'), value.get('op', value.get('operator', '==')), value.get('value')
    if not isinstance(key, str) or key.startswith(('event.', 'event_sequence.')) or 'value' not in value:
        raise MigrationError(f'{path}: pulse/unknown predicate is not a V2 state condition')
    if op not in ('==', '<', '<=', '>', '>=', 'contains') or not isinstance(target, (str, int, float, bool)):
        raise MigrationError(f'{path}: missing-field/coercion behavior is not provably equivalent')
    if key in ('process', 'process.name', 'process_name'):
        if op != '==':
            raise MigrationError(f'{path}: unsupported process comparison')
        return process(target, path)
    if op in ('<', '<=', '>', '>=') and (type(target) not in (int, float)):
        raise MigrationError(f'{path}: numeric coercion is not lossless')
    return {'field': key, 'op': op, 'value': target}


def migrate(source):
    result = copy.deepcopy(source)
    if not isinstance(result, dict):
        raise MigrationError('Expected config object')
    orch = result.setdefault('orchestration', {})
    fields(orch, ('version', 'rules', 'event_overlays', 'fallback_profile', 'automation_freshness_ms'), '/orchestration')
    for parent, key in ((result, 'rules'), (result, 'gsi_bindings'), (orch, 'rules'), (orch, 'event_overlays')):
        if not isinstance(parent.get(key, []), list):
            raise MigrationError(f'{key}: expected array')
    converted, diagnostics = [], []
    ids = set()
    def add(record, path):
        record = copy.deepcopy(record)
        record.setdefault('id', 'migrated-' + str(uuid.uuid5(uuid.NAMESPACE_URL, path + json.dumps(record, sort_keys=True))))
        record['model'] = 'automation_v2'
        if record['id'] in ids:
            raise MigrationError(f'{path}: duplicate ID')
        ids.add(record['id']); converted.append(record)
        diagnostics.append({'path': path, 'id': record['id']})
    def profile_rule(c, name, path, **metadata):
        if name not in result.get('profiles', {}):
            raise MigrationError(f'{path}: unresolved profile {name!r}')
        return {'when': {'mode': 'state', 'condition': c}, 'action': {'type': 'activate_profile', 'profile': name}, **metadata}
    # Preserve orchestration/V2 order, then old binding and Application tiers.
    for i, old in enumerate(orch.get('rules', [])):
        path = f'/orchestration/rules/{i}'
        if not isinstance(old, dict):
            raise MigrationError(f'{path}: expected rule object')
        if old.get('model') == 'automation_v2':
            add(old, path); continue
        fields(old, ('id', 'name', 'process', 'condition', 'target_profile', 'profile', 'dnd', 'suppress_web_ui', 'enabled'), path)
        predicates = []
        if old.get('process'): predicates.append(process(old['process'], path))
        if old.get('condition'):
            mapped = condition(old['condition'], path)
            def process_only(node):
                return node.get('field') == 'process.name' if 'field' in node else all(process_only(c) for children in node.values() for c in children)
            if not process_only(mapped):
                raise MigrationError(f'{path}: unbounded historical orchestration telemetry freshness requires owner rewrite')
            predicates.append(mapped)
        if not predicates:
            raise MigrationError(f'{path}: unconditional legacy rule requires explicit owner rewrite')
        c = predicates[0] if len(predicates) == 1 else {'and': predicates}
        metadata = {k: old[k] for k in ('id', 'name', 'enabled') if k in old}
        metadata['dnd'] = old.get('dnd', old.get('suppress_web_ui', False))
        add(profile_rule(c, old.get('target_profile', old.get('profile')), path, **metadata), path)
    cs2 = {'or': [process('cs2.exe', '/scope'), process('csgo.exe', '/scope')]}
    for i, old in enumerate(result.get('gsi_bindings', [])):
        path = f'/gsi_bindings/{i}'
        fields(old, ('field', 'operator', 'value', 'profile'), path)
        rule = profile_rule(condition({k: v for k, v in old.items() if k != 'profile'}, path), old.get('profile'), path)
        rule['scope'] = cs2; add(rule, path)
    seen_processes = set()
    for i, old in enumerate(result.get('rules', [])):
        path = f'/rules/{i}'
        fields(old, ('process', 'profile', 'suppress_web_ui'), path)
        c = process(old.get('process'), path)
        if old['process'] in seen_processes:
            raise MigrationError(f'{path}: duplicate process/DND arbitration needs owner review')
        seen_processes.add(old['process'])
        add(profile_rule(c, old.get('profile'), path, dnd=old.get('suppress_web_ui', False)), path)
    overlays = orch.get('event_overlays', [])
    for i, overlay in enumerate(overlays):
        if not isinstance(overlay, dict):
            raise MigrationError(f'/orchestration/event_overlays/{i}: expected object')
    priorities = [o.get('priority', 10) for o in overlays]
    if len(set(priorities)) != len(priorities):
        raise MigrationError('Equal-priority historical overlay activation order cannot be preserved')
    persistent = [o.get('priority', 10) for o in overlays if o.get('trigger') == 'state']
    transient = [o.get('priority', 10) for o in overlays if o.get('trigger', 'event') == 'event']
    if persistent and transient and max(persistent) >= min(transient):
        raise MigrationError('Historical interleaved overlay class order cannot be preserved')
    if overlays and any(r['action']['type'] == 'trigger_effect' for r in converted):
        raise MigrationError('Mixed V2 and historical overlay ordering requires owner review')
    for i, old in enumerate(overlays):
        path = f'/orchestration/event_overlays/{i}'
        fields(old, ('id', 'name', 'trigger', 'condition', 'event', 'effect', 'profile', 'priority', 'duration_ms', 'fade_ms', 'fade_out_ms', 'attack_ms', 'blend_mode'), path)
        name = old.get('effect', old.get('profile'))
        recipe = result.get('profiles', {}).get(name)
        if not recipe or recipe.get('type') == 'plugin':
            raise MigrationError(f'{path}: plugin lifecycle equivalence requires owner review')
        mode = old.get('trigger', 'event')
        if mode not in ('state', 'event'):
            raise MigrationError(f'{path}: unsupported trigger')
        blend = old.get('blend_mode', 'blend')
        if blend not in ('blend', 'add', 'replace'):
            raise MigrationError(f'{path}: unknown blend mode')
        if mode == 'state':
            if not old.get('condition'):
                raise MigrationError(f'{path}: state overlay requires an explicit condition')
            c = condition(old['condition'], path)
        else:
            event = old.get('event')
            if not isinstance(event, str) or not event.startswith('event.'):
                raise MigrationError(f'{path}: event must be canonical (daemon validates its identity)')
            c = {'event': event}
            if old.get('condition'): c = {'and': [c, condition(old['condition'], path)]}
        action = {'type': 'trigger_effect', 'effect': {'kind': 'profile_effect', 'name': name},
                  'lifetime': 'while_true' if mode == 'state' else 'one_shot', 'priority': old.get('priority', 10),
                  'composition': 'replace' if blend == 'replace' else 'overlay', 'blend': 'additive' if blend == 'add' else 'alpha'}
        if mode == 'event':
            duration = old.get('duration_ms', 1200)
            if type(duration) is not int or not 0 < duration <= 60000:
                raise MigrationError(f'{path}: duration exceeds supported one-shot lifetime')
            action.update(retrigger='restart', watchdog_ms=duration, compatibility={'lifecycle': 'legacy_envelope', 'duration_ms': duration,
                'fade_out_ms': old.get('fade_ms', old.get('fade_out_ms', 400)), 'attack_ms': old.get('attack_ms', 0)})
        rule = {'when': {'mode': mode, 'condition': c}, 'scope': cs2, 'action': action}
        rule.update({k: old[k] for k in ('id', 'name') if k in old}); add(rule, path)
    if result.get('gsi_bindings') or overlays:
        existing = orch.get('automation_freshness_ms')
        if existing is not None and existing != 10000:
            raise MigrationError('Historical 10-second GSI freshness conflicts with configured V2 freshness')
        orch['automation_freshness_ms'] = 10000
    if 'blockly_orchestrator' in result:
        diagnostics.append('Discarded obsolete Blockly editor cache; migrated runtime records are authoritative')
    result.pop('rules', None); result.pop('gsi_bindings', None); result.pop('blockly_orchestrator', None)
    orch.pop('event_overlays', None); orch['rules'] = converted
    return result, diagnostics


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--daemon', type=Path, required=True, help='Current aura_daemon.exe for production validation')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)
    try:
        if not args.dry_run and args.output is None: raise MigrationError('--output is required')
        if args.output and (args.output.resolve() == args.source.resolve() or args.output.exists()): raise MigrationError('Output must be a new file distinct from source')
        original = args.source.read_text(encoding='utf-8-sig')
        result, diagnostics = migrate(json.loads(original))
        rendered = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
        with tempfile.TemporaryDirectory(prefix='aura-migrate-') as temp:
            candidate = Path(temp) / 'config.json'; candidate.write_text(rendered, encoding='utf-8')
            validation = subprocess.run([str(args.daemon.resolve()), '--validate-config', str(candidate)], capture_output=True)
            if validation.returncode: raise MigrationError('Daemon validation failed: ' + (validation.stdout + validation.stderr).decode('utf-8', errors='replace'))
        if args.dry_run:
            sys.stdout.writelines(difflib.unified_diff(original.splitlines(True), rendered.splitlines(True), fromfile=str(args.source), tofile='V2-only config'))
        else:
            with args.output.open('x', encoding='utf-8', newline='\n') as output: output.write(rendered)
        print(json.dumps({'status': 'validated', 'mappings': diagnostics}, ensure_ascii=False), file=sys.stderr)
        return 0
    except (MigrationError, ValueError, TypeError, KeyError, OSError) as error:
        print(json.dumps({'status': 'refused', 'message': str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
