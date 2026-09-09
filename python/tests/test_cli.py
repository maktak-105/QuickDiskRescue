# -*- coding: utf-8 -*-
import json, os, subprocess, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIX = ROOT / "tests" / "fixtures"
CLI = ROOT / "dist" / "binary" / "QuickDiskRescue_cli.exe"


def run(*args):
    env = os.environ.copy()
    env["QUICKAPPSTEST"] = "1"
    p = subprocess.run([str(CLI), *args], capture_output=True, text=True, env=env, encoding="utf-8")
    print(p.stdout)
    print(p.stderr)
    assert p.returncode == 0, p.stderr
    return json.loads(p.stdout)


def test_gpt_mismatch():
    img = str(FIX / "gpt_mismatch.img")
    j = run("diagnose", "--source", img)
    assert j["ok"] is True
    assert j["gpt_primary"] is True
    assert j["size_mismatch"] is True
    assert j["partitions"]


def test_carve():
    img = str(FIX / "carve.img")
    out = str(ROOT / "build" / "carve_out")
    pathlib.Path(out).mkdir(parents=True, exist_ok=True)
    j = run("carve", "--source", img, "--out", out)
    assert j["ok"] is True
    assert j["count"] >= 1


if __name__ == "__main__":
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    import make_fixtures  # noqa: F401
    test_gpt_mismatch()
    test_carve()
    print("PASS")
