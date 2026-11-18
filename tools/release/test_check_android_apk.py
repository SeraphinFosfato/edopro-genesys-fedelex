"""Guardia APK nei due versi (FASE 121): python3 -m unittest discover tools/release"""
import tempfile
import unittest
import zipfile
from pathlib import Path

import check_android_apk as c

BUONO = {
    "assets/defaults/config/strings.conf": "x",
    "assets/defaults/config/configs.json": "{}",
    "assets/defaults/expansions/cards.cdb": "x",
    "assets/defaults/script/c1.lua": "x",
    "assets/index.txt": "config\n",
    "assets/filelist.txt": "config\n",
}


def apk(files):
    p = Path(tempfile.mkdtemp()) / "a.apk"
    with zipfile.ZipFile(p, "w") as z:
        for k, v in files.items():
            z.writestr(k, v)
    return p


class Guardia(unittest.TestCase):
    def test_verde(self):
        self.assertEqual(c.problemi(apk(BUONO)), [])

    def test_rosso_senza_strings(self):
        f = {k: v for k, v in BUONO.items() if "strings" not in k}
        self.assertEqual(c.problemi(apk(f)), ["manca assets/defaults/config/strings.conf"])

    def test_rosso_index_vuoto(self):
        self.assertEqual(len(c.problemi(apk({**BUONO, "assets/index.txt": ""}))), 1)
