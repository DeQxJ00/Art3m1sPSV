"""Ensure host UI literals have translations or an intentional bilingual path."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "host-direct" / "src"
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
CJK = re.compile(r"[\u4e00-\u9fff]")


class TranslationCoverage(unittest.TestCase):
    def test_host_literals(self):
        table = (SOURCE / "ui_translations.inc").read_text(encoding="utf-8")
        keys = set(re.findall(r'^\{"((?:[^"\\]|\\.)*)",', table, re.M))
        # Language selection must remain readable before a language is chosen.
        bilingual = {
            "选择语言 / Choose language",
            "之后可在设置中更改 / Change later in Settings",
            "↑↓ 选择 / Select    ○ 确认 / OK    × 退出 / Exit",
            "保存失败，请重试 / Save failed. Please retry.",
            "简体中文",
        }
        # This dialog selects its own complete English branch before rendering.
        notice = {
            "资源适配提示",
            "检测到 INI 或 TBL 中声明的分辨率高于 PSV 分辨率，资源尚未针对 PSV 进行适配。",
            "不确定如何压制资源？请参阅本项目 Art3m1sPsv 的 README，通常可以一键完成转换。",
            "不确定如何压制资源？请参阅本项目 ", " 的 README，通常可以一键完成转换。",
            "未进行转换可能导致以下问题：",
            "图片过大可能导致加载卡顿或卡死；E-mote 人物可能无法加载；",
            "OP 动画或 OGV 视频特效可能无法播放；Shader 或特效合成场景帧率偏低。",
            "即使能够进入游戏，后续仍可能出现其他问题，影响游戏体验。",
            "是否仍要继续进入游戏？", "○ 确认进入", "无法静态确认",
        }
        missing = []
        for path in SOURCE.rglob("*"):
            if path.suffix not in {".cpp", ".hpp", ".h"}:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                for value in LITERAL.findall(line):
                    if not CJK.search(value) or value in keys:
                        continue
                    if path.name == "language_menu.hpp" and value in bilingual:
                        continue
                    if path.name == "launcher_settings_menu.hpp" and value == "简体中文":
                        continue
                    if path.name == "resolution_notice.hpp" and value in notice:
                        continue
                    missing.append(f"{path.relative_to(SOURCE)}:{number}: {value}")
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
