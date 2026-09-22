import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
TOOL=ROOT/'tools/legacy/migrate_automation_v2.py'
spec=importlib.util.spec_from_file_location('migration',TOOL)
migration=importlib.util.module_from_spec(spec);spec.loader.exec_module(migration)
DAEMON=Path(sys.argv.pop(1)).resolve() if len(sys.argv)>1 and not sys.argv[1].startswith('-') else None


class MigrationTests(unittest.TestCase):
    def base(self):
        return {'default_profile':'base','hardware_backend':'auto','profiles':{'base':{'type':'static','color':[20,0,0]}},'general':{'preserve':True}}

    def test_process(self):
        source=self.base();source['rules']=[{'process':'cs2.exe','profile':'base','suppress_web_ui':True}]
        result,_=migration.migrate(source);r=result['orchestration']['rules'][0]
        self.assertEqual(r['when'],{'mode':'state','condition':{'field':'process.name','op':'==','value':'cs2.exe'}})
        self.assertEqual(r['action'],{'type':'activate_profile','profile':'base'});self.assertTrue(r['dnd'])
        self.assertIn('rules',source);self.assertNotIn('rules',result);self.assertEqual(result['profiles'],source['profiles'])
        self.assertEqual(result,migration.migrate(source)[0])

    def test_binding(self):
        source=self.base();source['gsi_bindings']=[{'field':'player.state.health','operator':'<','value':20,'profile':'base'}]
        result,_=migration.migrate(source);r=result['orchestration']['rules'][0]
        self.assertEqual(r['when']['condition']['value'],20);self.assertIn('scope',r);self.assertNotIn('gsi_bindings',result);self.validate(result)

    def test_overlays(self):
        for mode in ('state','event'):
            with self.subTest(mode=mode):
                source=self.base();overlay={'trigger':mode,'effect':'base','priority':20}
                if mode=='state':overlay['condition']={'field':'player.state.health','op':'<','value':20}
                else:overlay.update(event='event.kill',duration_ms=800,fade_out_ms=200,attack_ms=20)
                source['orchestration']={'event_overlays':[overlay]}
                result,_=migration.migrate(source);r=result['orchestration']['rules'][0]
                self.assertEqual(r['when']['mode'],mode);self.assertEqual(r['action']['lifetime'],'while_true' if mode=='state' else 'one_shot')
                if mode=='event':self.assertEqual(r['action']['compatibility']['fade_out_ms'],200)
                self.assertNotIn('event_overlays',result['orchestration'])
                self.validate(result)

    def validate(self,config):
        self.assertIsNotNone(DAEMON,'Supply the current daemon binary; validation must not be skipped')
        with tempfile.TemporaryDirectory() as temp:
            file=Path(temp)/'config.json';file.write_text(json.dumps(config),encoding='utf-8')
            run=subprocess.run([str(DAEMON),'--validate-config',str(file)],capture_output=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_refuses_unknown_and_lossy(self):
        source=self.base();source['rules']=[{'process':'cs2.exe','profile':'base','unknown':True}]
        with self.assertRaisesRegex(migration.MigrationError,'unmapped'):migration.migrate(source)
        source=self.base();source['gsi_bindings']=[{'field':'absent','operator':'!=','value':1,'profile':'base'}]
        with self.assertRaises(migration.MigrationError):migration.migrate(source)
        source=self.base();source['orchestration']={'event_overlays':[{'trigger':'state','priority':30},{'trigger':'event','priority':20}]}
        with self.assertRaisesRegex(migration.MigrationError,'order'):migration.migrate(source)

    def test_refuses_ambiguous_aliases_and_unbounded_telemetry(self):
        source=self.base();source['orchestration']={'rules':[{'condition':{'field':'health','value':20},'profile':'base'}]}
        with self.assertRaisesRegex(migration.MigrationError,'freshness'):migration.migrate(source)
        source=self.base();source['gsi_bindings']=[{'field':'health','operator':'==','value':20,'profile':'base','unknown':0}]
        with self.assertRaises(migration.MigrationError):migration.migrate(source)
        with self.assertRaisesRegex(migration.MigrationError,'conflicting'):migration.condition({'field':'health','op':'==','operator':'<','value':20},'/test')

    def test_cli_never_changes_source_and_validates_output(self):
        self.assertIsNotNone(DAEMON)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'source.json';output=Path(temp)/'v2.json'
            config=self.base();config['rules']=[{'process':'cs2.exe','profile':'base'}]
            source.write_text(json.dumps(config),encoding='utf-8');before=source.read_bytes()
            command=[sys.executable,str(TOOL),str(source),'--daemon',str(DAEMON)]
            dry=subprocess.run(command+['--dry-run'],capture_output=True)
            self.assertEqual(dry.returncode,0,dry.stderr);self.assertIn(b'---',dry.stdout);self.assertFalse(output.exists())
            run=subprocess.run(command+['--output',str(output)],capture_output=True)
            self.assertEqual(run.returncode,0,run.stderr);self.assertEqual(source.read_bytes(),before)
            result=json.loads(output.read_text());self.validate(result)
            self.assertTrue(all(r['model']=='automation_v2' for r in result['orchestration']['rules']))
            self.assertNotEqual(subprocess.run(command+['--output',str(source)],capture_output=True).returncode,0)
            self.assertEqual(source.read_bytes(),before)

    def test_daemon_rejects_each_retired_section_with_path(self):
        for path in ('rules','gsi_bindings','orchestration/event_overlays','orchestration/rules'):
            with self.subTest(path=path),tempfile.TemporaryDirectory() as temp:
                config=self.base();parts=path.split('/');parent=config
                for part in parts[:-1]:parent=parent.setdefault(part,{})
                parent[parts[-1]]=[{'process':'cs2.exe','profile':'base'}]
                file=Path(temp)/'bad.json';file.write_text(json.dumps(config))
                run=subprocess.run([str(DAEMON),'--validate-config',str(file)],capture_output=True)
                self.assertNotEqual(run.returncode,0);self.assertIn(b'migration_required',run.stdout+run.stderr)
                self.assertIn(path.encode(),run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
