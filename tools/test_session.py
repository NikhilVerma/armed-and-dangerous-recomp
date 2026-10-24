"""Unit tests for tools/session.py: a session's summary and a bookmark turned into a playtest script.

    python -m unittest tools/test_session.py
"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import session  # noqa: E402


def write_jsonl(path, rows):
    with open(path, 'w', encoding='utf-8') as f:
        for r in rows:
            f.write(json.dumps(r) + '\n')


class SessionTool(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name
        self.name = '2026-10-18_14-03-22'
        self.sdir = os.path.join(self.root, self.name)
        os.makedirs(os.path.join(self.sdir, 'bookmarks', '001'))
        os.makedirs(os.path.join(self.root, '2026-10-17_09-00-00'))
        os.makedirs(os.path.join(self.root, 'not_a_session'))
        write_jsonl(os.path.join(self.sdir, 'events.jsonl'), [
            {'t': 3.0, 'text': 'session: level lev_1_m1 started (generation 0)'},
            {'t': 40.0, 'text': 'dev: god on'},
            {'t': 90.0, 'text': 'session: Roman died on lev_1_m1'},
            {'t': 95.0, 'text': 'session: mission Failed on lev_1_m1: You died'},
            {'t': 120.0, 'text': 'session: level lev_1_m1_b started (generation 2)'},
            {'t': 300.5, 'text': 'session: mission Won on lev_1_m1_b'},
        ])
        write_jsonl(os.path.join(self.sdir, 'frames.jsonl'), [
            {'t': 5, 'state': 'Playing', 'level': 'lev_1_m1', 'frames': 500, 'mean_ms': 10.0, 'p99_ms': 12, 'max_ms': 20, 'over_33ms': 0, 'over_100ms': 0},
            {'t': 10, 'state': 'Playing', 'level': 'lev_1_m1', 'frames': 250, 'mean_ms': 20.0, 'p99_ms': 80, 'max_ms': 150, 'over_33ms': 4, 'over_100ms': 1},
            {'t': 15, 'state': 'Loading', 'level': 'lev_1_m1_b', 'frames': 10, 'mean_ms': 400.0, 'p99_ms': 900, 'max_ms': 900, 'over_33ms': 10, 'over_100ms': 9},
        ])
        with open(os.path.join(self.sdir, 'game.log'), 'w') as f:
            f.write('     1.000 INFO  remaster: hello\n     2.000 WARN  remaster: odd\n     3.000 ERROR remaster: bad\n')
        with open(os.path.join(self.sdir, 'bookmarks', '001', 'save.json'), 'w') as f:
            f.write('{}')
        write_jsonl(os.path.join(self.sdir, 'bookmarks.jsonl'), [
            {'n': 1, 'level': 'lev_1_m1', 'pos': [1, 2, 3], 'note': 'Q stuck here', 'screenshot': 'bookmarks/001/screenshot.png', 'save': 'bookmarks/001/save.json'},
            {'n': 2, 'level': 'lev_shell', 'note': '', 'screenshot': 'bookmarks/002/screenshot.png', 'save': None},
        ])

    def tearDown(self):
        self.tmp.cleanup()

    def test_sessions_and_find(self):
        self.assertEqual(session.sessions(self.root), [self.name, '2026-10-17_09-00-00'])
        self.assertEqual(session.find(self.root, 'latest'), self.name)
        self.assertEqual(session.find(self.root, '2026-10-17'), '2026-10-17_09-00-00')

    def test_summary(self):
        s = session.summarise(self.sdir)
        self.assertEqual(s['levels'], ['lev_1_m1', 'lev_1_m1_b'])
        self.assertEqual((s['deaths'], s['won'], s['failed']), (1, ['lev_1_m1_b'], ['lev_1_m1: You died']))
        self.assertEqual(s['dev'], ['god on'])
        self.assertEqual((s['warnings'], s['errors'], s['panic']), (1, 1, None))
        # loading frames are left out of the play statistics
        self.assertEqual((s['frames_playing'], s['mean_ms_playing'], s['over_33ms'], s['over_100ms']), (750, 13.33, 4, 1))
        self.assertEqual(s['duration_s'], 300.5)
        self.assertEqual([b['note'] for b in s['bookmarks']], ['Q stuck here', ''])

    def test_bookmark_to_script(self):
        marks = session.jsonl(os.path.join(self.sdir, 'bookmarks.jsonl'))
        script = session.bookmark_to_script(marks[0], self.sdir, self.name)
        self.assertTrue(os.path.isabs(script['load']) and script['load'].endswith('bookmarks/001/save.json'))
        self.assertTrue(os.path.exists(script['load']))
        self.assertIn('Q stuck here', script['comment'])
        self.assertEqual(script['steps'][-1], {'quit': True})
        self.assertIn({'expect': {'campaign.level': 'lev_1_m1'}}, script['steps'])
        with self.assertRaises(ValueError):
            session.bookmark_to_script(marks[1], self.sdir, self.name)

    def test_repro_command(self):
        out = os.path.join(self.root, 'repro.json')
        self.assertEqual(session.main(['repro', 'latest', '1', '--root', self.root, '--out', out]), 0)
        with open(out) as f:
            self.assertEqual(json.load(f)['load'], session.bookmark_to_script(session.jsonl(os.path.join(self.sdir, 'bookmarks.jsonl'))[0], self.sdir, self.name)['load'])


if __name__ == '__main__':
    unittest.main()
