星风观测站 / STARWIND OBSERVATORY
Art3m1sPSV 原创演示工程
这是一个由 AI 生成的 art3m1s 引擎演示 Demo。

游戏目录
game/STARWIND_DEMO：23页完整演示，普通PNG立绘、眼嘴小图差分、独立头像、
灰度/褐色/马赛克/模糊平移、昼夜转场、配套遮罩OGV、E-mote和日语语音。

使用
开篇用 ↑↓ 选择简体中文、日本語或 English，○ 确认。
对白、章节、演示说明和Demo自带的存读档/Backlog界面随语言切换；日语配音不变。
存档会记录语言；没有语言字段的旧存档沿用当前选择。完整演示结束后可重新选语言。
宿主程序的方块菜单、启动器等仍使用宿主自身的界面语言。
把上述整个游戏文件夹复制到 ux0:data/art3m1s-gxm/games/，在Art3m1sPSV中选择。
发行包为PFS格式：解压dist中的ZIP后复制其游戏文件夹，资源集中在root.pfs中。
system.ini、脚本、三语文本、字体、图片、声音、OGV及遮罩、PSB均从PFS读取。
图标、标题和授权说明在包外另保留一份，便于启动器识别及查看许可。
升级已有散文件版时，请先移走旧游戏文件夹，再复制新包，避免旧散文件覆盖PFS内容。
工程game/中的散文件继续作为可编辑源文件保留，不会在打包时被删除。
外部游戏包不是VPK，不会替换主程序。开发时使用960×544画面。
○ 下一页。□ 宿主菜单，× 返回。
已增加存读档、快速存读档、Backlog及四页操作引导，并通过模拟器操作验证。
□ 菜单中选择存档或读档，↑ ↓ 选择三个普通槽位，○ 保存/覆盖或读取。
← 快速存档，→ 快速读档。快速槽位与普通槽位分离。
△ 或 ↑ 打开Backlog，↑ ↓ 翻阅，○ 重播所选记录的语音，× 返回。
存档使用引擎原生文件，保留当前对话页及阅读记录；读档后重建人物和效果，
该句语音及持续动画从本页开头重新播放。

人物
澄夏（すみか），原创角色。普通立绘身体384×576；眼部94×48、嘴部38×26。
头像另有128×144底图，眼嘴使用独立坐标的小差分，不与立绘混用。
E-mote是实际PSB v2文件，通过引擎E-mote加载器播放，不是影片伪装的动画。
当前模型为轻量分层示例：身体整体呼吸/摆动、眨眼、微笑/惊讶和语音振幅嘴型。
它没有复杂网格形变、头发物理或逐音素口型。PSB约2.01 MiB，RGBA8图集512×1024。
图集包含双像素边缘扩展，避免眼嘴小图在双线性采样时出现接缝。

来源
人物、背景、图标由图像生成工具为本演示创作，原图与生成记录保留在art/。
日语语音：VOICEVOX:ナースロボ＿タイプＴ（ノーマル）。采用用户选定的自然音高，
语速0.98、抑扬0.95；具体配置与嘴型包络见audio-manifest.json。
第二段E-mote按20ms语音能量轨迹开合嘴部，40ms最短保持抑制抖动，停顿及时闭嘴。
完整演示末尾有语音署名页；游戏包附简短说明VOICE-CREDITS.txt。
来源：https://voicevox.hiroshiba.jp/
声库规则：https://www.krnr.top/rules
免费使用须保留署名并遵守官方规则，不能将音频当作无条件授权素材重新发布。
中文台词、日文台词、剧情脚本、简易音乐及粒子动画均为本演示编写。
字体沿用Art3m1sPSV演示使用的字体，许可证见游戏内licenses/FONT.txt。
着色器沿用Art3m1sPSV已有的Artemis效果公式；不要求运行时编译新shader。

构建顺序
python scripts/build_assets.py
python scripts/generate_audio.py
python scripts/generate_particles.py
python scripts/build_story.py
python scripts/build_emote_demo.py
python scripts/package_demo.py
依赖Python/Pillow、VOICEVOX Core及官方模型、FFmpeg；粒子编码脚本使用本机WSL的libtheora。
构建脚本保留开发机工具路径；换机器时需调整字体和编码器路径。
PFS打包复用相邻art3m1s_psv_port_tool工程的PfsCodec，需.NET 10 SDK；
package_demo.py将逐项解包并核对SHA-256，确认无损后生成ZIP。
三语文案位于scripts/demo_locales.py及scripts/ui_locales.json。
正文与姓名通过mw_getmsgid登记，字号设置由引擎统一调整并重新排版。
需要Art3m1sPSV v1.3.7或更新版本，以正确处理英文空格。
更换font.ttf后需安装fonttools并运行python scripts/generate_font_metrics.py更新字宽表。

验证范围
E-mote在Vita3K中验证：PSB解析、人物层次、眨眼、语音开闭嘴、惊讶、摆动、
三页往返循环及宿主菜单返回。模拟器帧率不能代替实机性能验收。
存档槽位显示章节、对话与预览区域。原生截图接口已接入，但本机Vita3K回读PNG仍为黑图，
这一项未通过；章节/台词及实际读档已单独验证，实机缩略图尚待验证。

temp/保存截图、日志和模型检查结果；backup/保存测试前环境和旧音频。
