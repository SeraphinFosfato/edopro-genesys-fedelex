"""Guardia A nei due versi (FASE 120 cancello 3): python3 -m unittest discover tools/release"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_linux_libs as c

BASE = {"libfmt.so.9", "libgit2.so.1.7"}


@unittest.skipUnless(shutil.which("gcc"), "serve gcc")
class Guardia(unittest.TestCase):
    def elf(self, soname):
        d = Path(tempfile.mkdtemp())
        (d / "x.c").write_text("int f(void){return 0;}")
        (d / "m.c").write_text("int f(void);int main(){return f();}")
        subprocess.run(["gcc", "-shared", "-o", d / soname, d / "x.c", f"-Wl,-soname,{soname}"], check=True)
        subprocess.run(["gcc", "-o", d / "exe", d / "m.c", d / soname], check=True)
        return d / "exe"

    def test_fmt10_rosso_col_nome(self):
        self.assertEqual(c.mancanti(self.elf("libfmt.so.10"), BASE), ["libfmt.so.10"])

    def test_fmt9_verde(self):
        self.assertEqual(c.mancanti(self.elf("libfmt.so.9"), BASE), [])
