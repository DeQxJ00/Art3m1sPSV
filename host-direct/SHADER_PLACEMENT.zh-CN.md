# Shader 放置说明

## 普通游戏：通常不用额外放文件

最新版引擎已内置 game1、game2 的 51 个来源文件对应的 31 种独立效果。游戏原来的 HLSL 文件仍需保留在资源包或原资源目录中，程序按源码内容匹配内置效果；匹配成功时无需额外 CG/GXP，也不会为这些效果生成编译缓存。

游戏选择界面 → START 设置中的“Shader 自动转换”和“Shader 自动编译”默认均关闭，一般无需开启。已有保存设置按用户选择读取。

不要为了放 shader 而把整个 PFS 解包到正在使用的游戏目录中。PC 上的 `work/steam_unpack` 等分析目录不是缓存路径的一部分。

## 外置效果：缓存放在对应游戏目录内

假设脚本使用的源码路径为 `system/shader/pc/example.hlsl`，对应实机位置为：

```text
ux0:data/art3m1s-gxm/games/<游戏文件夹>/
├─ 游戏资源包.pfs                     ← 原始 HLSL 可继续留在包内
└─ shader-cache/
   └─ system/shader/pc/
      ├─ example.hlsl.cg
      ├─ example.hlsl.conversion.json
      ├─ example.hlsl.gxp
      └─ example.hlsl.hash
```

缓存目录遵循“`shader-cache/` + 脚本读取源码的相对路径 + 缓存后缀”。上面的 `pc` 保留原始资源路径，并不表示这些 CG/GXP 给 PC 使用；不要自行改成 `psv`。如果脚本请求其他目录，就按实际路径对应，不要一律放进 `system/shader/pc`。

保留原文件后缀：`example.hlsl.cg` 不能改成 `example.cg`。普通游戏缓存放在各自游戏目录下，不放在 VPK 的应用目录或原 PFS 内。单独部署的 demo 按普通游戏处理，缓存写入该 demo 文件夹下的 `shader-cache/`。

| 文件 | 内容及配套要求 |
|---|---|
| `.hlsl.cg` | 转换后的 Cg 源码，尚未编译成 GPU 程序。 |
| `.hlsl.conversion.json` | 与 Cg、原始 HLSL 对应的参数布局和校验信息，必须与 Cg 配套。 |
| `.hlsl.gxp` | PSV 编译后的 GPU 程序。 |
| `.hlsl.hash` | 与 GXP、源码及编译环境对应的校验信息，必须与 GXP 配套。 |

## 全局共享 Shader 支持目录（不放在 games 内）

可将已验证的新增效果缓存放在：

```text
ux0:data/art3m1s-gxm/shader-cache/
└─ system/shader/pc/
   ├─ example.hlsl.cg
   ├─ example.hlsl.conversion.json
   ├─ example.hlsl.gxp
   └─ example.hlsl.hash
```

共享目录与 `games` 同级，不会被识别成游戏。路径仍须与脚本请求的原始 HLSL 相对路径一致；不添加游戏文件夹名。旧版内置 demo 留下的 `TEST_SHADERS_51/`、`TEST_SHADERS_EXTERNAL/` 是历史专属缓存，不属于共享路径；当前外置 demo 使用自身文件夹下的缓存。

加载顺序：匹配内置效果 → 游戏专属缓存 → 全局共享缓存 → 按开关允许转换/编译。Cg 与 GXP 分别检查，专属缓存有效时优先使用；专属项缺少、过期或校验失败时可用匹配的共享项。共享文件同样校验原始 HLSL、Cg、ABI、编译器和 GXP 内容，不能只靠同名复用。不同游戏的源码及相对路径相同时可共享；同名但源码不同不会误用。

完整四文件有效时，两个开关都关闭也能使用。只有 Cg＋配套元数据时，需开启自动编译；生成的 GXP 仍写入当前游戏的专属缓存。程序会创建共享根目录，但不会自动发布、修改或清理其中的支持文件，以免不同游戏相互覆盖。可将一次成功编译的四文件按原路径复制到这里；不要仅放任意裸 Cg/GXP，也不要删除游戏原始 HLSL。

新增共享文件或替换文件后，退出当前游戏再进入生效。未支持的 HLSL 不会因为放进共享目录就自动变为支持，需要有效的配套转换结果及参数布局。

## 根据已有文件选择开关

| 你已有的资源 | 自动转换 | 自动编译 | 处理方式 |
|---|---|---|---|
| 能匹配内置效果的原始 HLSL | 关 | 关 | 直接使用内置程序，不需要手工放缓存。 |
| 匹配的完整四个缓存文件，以及对应原始 HLSL | 关 | 关 | 读取已有转换和编译结果。 |
| 匹配的 Cg＋conversion.json，以及对应原始 HLSL | 关 | 开 | 在 PSV 上编译并保存 GXP＋hash。 |
| 只有未匹配内置效果的 HLSL | 按需开 | 按需开 | 仅支持可转换的 HLSL 子集；首次成功后可关闭两个开关，保留缓存复用。 |

需要在 PSV 上自动编译时，需安装 `ur0:/data/libshacccg.suprx`。开关修改后，下次进入游戏生效。

当前不能仅放一个任意 `.cg`、裸 `.gxp` 或离线工具生成的 `.agxp`，就让游戏自动识别。请使用与该原始 HLSL 配套、由当前流程生成并验证过的缓存。修改源码、Cg、参数布局或更换编译器后，旧缓存可能不再匹配；不要通过改名或手改校验文件强行复用。
