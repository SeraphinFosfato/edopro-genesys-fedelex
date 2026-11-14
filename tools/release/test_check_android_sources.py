"""Il controllo fallisce nei due versi (FASE 119 cancello 3): python3 -m unittest discover tools/release"""
import tempfile
import unittest
from pathlib import Path

import check_android_sources as c


def finto(mk_voci, solo_desktop, sorgenti):
    d = Path(tempfile.mkdtemp())
    (d / "gframe").mkdir()
    for s in sorgenti:
        (d / "gframe" / s).parent.mkdir(parents=True, exist_ok=True)
        (d / "gframe" / s).write_text("")
    (d / "android" / "jni").mkdir(parents=True)
    (d / "android" / "jni" / "Android.mk").write_text(
        "".join(f"  $(GFRAME_PATH)/{v} \\\n" for v in mk_voci))
    (d / "tools" / "release").mkdir(parents=True)
    (d / "tools" / "release" / "solo_desktop.txt").write_text(solo_desktop)
    return d


class Controllo(unittest.TestCase):
    def test_allineato_e_verde(self):
        d = finto(["a.cpp"], "b.cpp\tmotivo\n", ["a.cpp", "b.cpp", "lzma/x.c"])
        self.assertEqual(c.controlla(d), [])

    def test_cpp_nuovo_non_elencato_e_rosso(self):
        d = finto(["a.cpp"], "", ["a.cpp", "nuovo.cpp"])
        self.assertTrue(any("nuovo.cpp" in e for e in c.controlla(d)))

    def test_voce_morta_e_rossa(self):
        d = finto(["a.cpp"], "morto.cpp\tmotivo\n", ["a.cpp"])
        self.assertTrue(any("morto.cpp" in e for e in c.controlla(d)))

    def test_voce_contraddittoria_e_rossa(self):
        d = finto(["a.cpp"], "a.cpp\tmotivo\n", ["a.cpp"])
        self.assertTrue(any("contraddittoria" in e for e in c.controlla(d)))

    def test_riga_senza_motivo_fallisce(self):
        d = finto(["a.cpp"], "a.cpp\n", ["a.cpp"])
        with self.assertRaises(SystemExit):
            c.controlla(d)


if __name__ == "__main__":
    unittest.main()
