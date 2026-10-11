import json
import os
import unittest

from refbuild import rules
from refbuild.normalize import Config, Skip, normalize, number, tokens, vdu_key

HERE = os.path.dirname(os.path.abspath(__file__))


def shared():
    with open(os.path.join(HERE, 'normalize.json')) as f:
        data = json.load(f)
    c = data['config']
    cfg = Config([Skip(s['prefix'], s['units']) for s in c['skips']], c['hex_after'])

    return data, cfg


class Shared(unittest.TestCase):
    """tests/normalize.json, which the reader core's tests run too."""

    def test_normalize(self):
        data, cfg = shared()
        for text, want in data['normalize']:
            with self.subTest(text=text):
                self.assertEqual(normalize(text, cfg), want)

    def test_vdu_key(self):
        data, cfg = shared()
        for text, want in data['vdu_key']:
            with self.subTest(text=text):
                self.assertEqual(vdu_key(text, cfg), want)

    def test_config_is_the_rules(self):
        # The shared cases are run with the settings rules.toml gives, so the
        # C is tested against what ref.dat will carry.
        _, cfg = shared()
        r = rules.load(os.path.join(os.path.dirname(HERE), 'rules.toml'))
        self.assertEqual(r.normalize, cfg)


class Pieces(unittest.TestCase):
    def test_wide(self):
        self.assertEqual(tokens('1; 2 ;3,4'), [('1', True), ('2', True), ('3', False), ('4', False)])

    def test_numbers(self):
        self.assertEqual(number('&ff'), 255)
        self.assertEqual(number('ff'), None)
        self.assertEqual(number('ff', hex_context=True), 255)
        self.assertEqual(number('10', hex_context=True), 16)
        self.assertEqual(number('1_1'), None)        # base 1
        self.assertEqual(number('2_102'), None)      # 2 is not a binary digit

    def test_longest_skip_wins(self):
        cfg = Config([Skip(['vdu', '23'], [1]), Skip(['vdu', '23', '0'], [1, 1])])
        self.assertEqual(normalize('vdu 23 0 5 6 7', cfg), 'vdu 23 0 _ _ 7')
        self.assertEqual(normalize('vdu 23 1 5', cfg), 'vdu 23 _ 5')


if __name__ == '__main__':
    unittest.main()
