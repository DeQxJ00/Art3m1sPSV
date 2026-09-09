//! 场景树：点分层级 ID 管理的保留模式图层集合。
//!
//! Artemis 用点分 ID 表达图层层级：`"1"` 是根组，`"1.0"` 是它的子层，`"1.0.-1"`
//! 再下一级。父层的变换/不透明度向下继承，`[lydel id="1.0"]` 删除整棵子树，
//! `[lyprop id="1.0"]` 对整个组批量设属性。本模块只维护这棵树的结构与每个节点
//! 的属性/动画，不涉及任何绘制——绘制由 `build` 遍历这棵树产出。

use crate::compositor::anim::Tween;
use crate::compositor::props::LayerProps;
use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::HashMap;
use std::sync::OnceLock;
mod order;
use order::{OrderedIds, TraversalOrder};

/// 独立消息层使用的内部根节点前缀。
///
/// Artemis 规定未指定 `chgmsg layered`（或指定 `layered=0`）的消息层显示在所有
/// 图像层之上。运行时把这类消息层映射到此前缀下；排序时显式置于普通图层之后。
pub const MESSAGE_LAYER_OVERLAY_PREFIX: &str = "@art3m1s-message-";

/// 一个图层节点。
///
/// 节点既可能绑定了纹理资源（`file`），也可能只是一个用于分组/变换的空容器
/// （只设了属性、没有 `file`）。
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct Layer {
    /// 完整点分 ID，如 `"1.0.-1"`。
    pub id: String,
    /// 绑定的逻辑资源名；`None` 表示纯分组节点。
    pub file: Option<String>,
    /// `lyc` 缺省 file 的单色图层模式：RGBA 填充色（宽高取 props.width/height）。
    /// 与 `file` 互斥——设置了 `file` 时忽略。
    #[serde(default)]
    pub solid_color: Option<[u8; 4]>,
    /// `lyc` 的 mask 参数：蒙版图路径。绘制时经
    /// [`TextureProvider::resolve_with_mask`](crate::render_pipeline::draw::TextureProvider::resolve_with_mask)
    /// 与 `file` 合成 alpha。
    #[serde(default)]
    pub mask: Option<String>,
    pub props: LayerProps,
    /// 作用在本节点属性上的进行中缓动。
    pub tweens: Vec<Tween>,
    /// 直接子节点的完整 ID，按插入顺序保存以保证稳定的绘制次序。
    pub children: Vec<String>,
    /// [lyevent] 注册的事件处理器，按事件类型（click/rollover/rollout/...）索引。
    /// 引擎只负责命中后把对应处理器交还解释器执行，不解释其内容。
    pub event_handlers: HashMap<String, LayerEventHandler>,
}

/// 一个 [lyevent] 注册的图层事件处理器。
///
/// 完全对应 Artemis `lyevent` 标签语义：命中后引擎执行 `handler` 标签（若有），
/// 并跳转/调用到 `(file, label)`（若有），其余参数原样透传。引擎不认识其中任何
/// 游戏函数名或参数含义。
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct LayerEventHandler {
    /// `mode=disable` only suspends dispatch; Artemis keeps the registered
    /// callback and its parameters so a later `mode=enable` can restore it.
    #[serde(default = "event_handler_enabled_by_default")]
    pub enabled: bool,
    /// 命中时先就地执行的标签名（如 `"calllua"`）；`None` 表示不执行内联标签。
    pub handler: Option<String>,
    /// 跳转/调用目标脚本文件；与 jump/call 标签的 file 参数同义。
    pub file: Option<String>,
    /// 跳转/调用目标标签；与 jump/call 标签的 label 参数同义。
    pub label: Option<String>,
    /// `call=1` 时把当前执行位置压入调用栈（对应 call 标签），否则等同 jump。
    pub call: bool,
    /// 图层重叠时是否穿透到下层（penetration=1）。
    pub penetration: bool,
    /// lyevent 标签里除已知字段外的所有参数（function、name、key、se 等），
    /// 触发时原样塞进 handler 标签的参数表。
    pub params: HashMap<String, String>,
    /// 注册事件时的完整标签参数，供 `e:setEventFilter` 原样检查。
    ///
    /// 旧存档没有此字段；派发侧会退回到 `params` 并补齐必要字段。
    #[serde(default)]
    pub filter_params: HashMap<String, String>,
}

fn event_handler_enabled_by_default() -> bool {
    true
}

impl Layer {
    fn new(id: String) -> Self {
        Self {
            id,
            ..Default::default()
        }
    }
}

/// Artemis 图层 ID 排序：按点号分割，数字部分按数值比较，字符串部分按字典序。
/// 数字部分优先于字符串部分（数字在前）。
fn compare_layer_id(a: &str, b: &str) -> Ordering {
    match (
        a.starts_with(MESSAGE_LAYER_OVERLAY_PREFIX),
        b.starts_with(MESSAGE_LAYER_OVERLAY_PREFIX),
    ) {
        (true, false) => return Ordering::Greater,
        (false, true) => return Ordering::Less,
        _ => {}
    }

    let mut parts_a = a.split('.');
    let mut parts_b = b.split('.');

    loop {
        match (parts_a.next(), parts_b.next()) {
            (None, None) => return Ordering::Equal,
            (None, Some(_)) => return Ordering::Less,
            (Some(_), None) => return Ordering::Greater,
            (Some(pa), Some(pb)) => {
                let ord = compare_id_part(pa, pb);
                if ord != Ordering::Equal {
                    return ord;
                }
            }
        }
    }
}

/// 比较单个 ID 部分：数字按数值，字符串按字典序，数字优先于字符串。
fn compare_id_part(a: &str, b: &str) -> Ordering {
    let a_num: Option<i64> = a.parse().ok();
    let b_num: Option<i64> = b.parse().ok();

    match (a_num, b_num) {
        (Some(na), Some(nb)) => na.cmp(&nb),
        (Some(_), None) => Ordering::Less,    // 数字优先
        (None, Some(_)) => Ordering::Greater, // 字符串在后
        (None, None) => a.cmp(b),             // 都是字符串，按字典序
    }
}

/// 整棵场景树。
///
/// 节点存在扁平的 `HashMap` 里（键为完整 ID），父子关系通过 ID 推导，子节点顺序
/// 单独记录。根节点集合是没有父级的顶层 ID，按插入顺序排列。
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct Scene {
    nodes: HashMap<String, Layer>,
    /// 顶层节点 ID，按插入顺序——决定根层之间的绘制先后。
    roots: Vec<String>,
    /// `[lyprop id="!"]` 操作的"包含所有图层的根图层"的属性。
    /// 变换/不透明度/可见性作用于整棵场景树。
    #[serde(default)]
    root_props: LayerProps,
    #[serde(skip)]
    traversal_order: TraversalOrder,
    #[serde(skip)]
    order_cache_disabled: bool,
}

impl Scene {
    pub fn new() -> Self {
        Self::default()
    }

    /// Snapshot for transition rendering only. Event handlers are needed by
    /// the live scene and saves, but never by draw-list construction. Avoid
    /// copying their script parameter maps at every rendered frame.
    pub fn render_snapshot(&self) -> Self {
        Self {
            roots: self.roots.clone(),
            root_props: self.root_props.clone(),
            traversal_order: TraversalOrder::default(),
            order_cache_disabled: self.order_cache_disabled,
            nodes: self.nodes.iter().map(|(id, layer)| (id.clone(), Layer {
                id: layer.id.clone(), file: layer.file.clone(), mask: layer.mask.clone(),
                solid_color: layer.solid_color, props: layer.props.clone(),
                tweens: layer.tweens.clone(), children: layer.children.clone(),
                event_handlers: HashMap::new(),
            })).collect(),
        }
    }

    pub fn replace_with(&mut self, other: Scene) {
        *self = other;
    }

    pub fn get(&self, id: &str) -> Option<&Layer> {
        self.nodes.get(id)
    }

    pub fn get_mut(&mut self, id: &str) -> Option<&mut Layer> {
        // The public children vector can be replaced or reordered through this
        // borrow, even when the caller intended to change only a property.
        self.traversal_order.invalidate_children(id);
        self.nodes.get_mut(id)
    }

    pub fn set_file(&mut self, id: &str, file: Option<String>) {
        self.ensure_path(id);
        if let Some(layer) = self.nodes.get_mut(id) {
            layer.file = file;
        }
    }

    pub fn clear_file_if_matches(&mut self, id: &str, expected: &str) {
        if let Some(layer) = self.nodes.get_mut(id)
            && layer.file.as_deref() == Some(expected)
        {
            layer.file = None;
        }
    }

    /// 设置图层的蒙版图路径（`lyc` mask 参数）。空字符串/None 表示清除。
    pub fn set_mask(&mut self, id: &str, mask: Option<String>) {
        self.ensure_path(id);
        if let Some(layer) = self.nodes.get_mut(id) {
            layer.mask = mask.filter(|m| !m.is_empty());
        }
    }

    /// 把图层设为单色模式（`lyc` 缺省 file + color）。会清除已绑定的 file。
    /// 宽高由调用方通过 props 的 width/height 设置。
    pub fn set_solid_color(&mut self, id: &str, rgba: Option<[u8; 4]>) {
        self.ensure_path(id);
        if let Some(layer) = self.nodes.get_mut(id) {
            layer.solid_color = rgba;
            if rgba.is_some() {
                layer.file = None;
            }
        }
    }

    /// `[lyprop id="!"]`：根图层属性（作用于整棵场景树）。
    pub fn root_props(&self) -> &LayerProps {
        &self.root_props
    }

    /// 图层是否"有效可见"：自身 `visible != false`，且从根到它的祖先链上
    /// 每一层都可见，且根图层（`!`）可见。与命中检测/绘制的可见性剔除一致。
    ///
    /// 用于 link 命中检测：mw 隐藏（自身或祖先 visible=0）后，其文本层的
    /// link 命中区必须失效，否则点在已隐藏的文本区会误触发链接跳转。
    /// 图层不存在时视为不可见。
    pub fn is_effectively_visible(&self, id: &str) -> bool {
        // 目标层本身必须存在。
        if self.nodes.get(id).is_none() {
            return false;
        }
        self.existing_path_is_visible(id)
    }

    /// 判断逻辑 ID 路径上所有已存在节点是否可见。
    ///
    /// 独立消息层虽然绘制在内部 overlay 根节点上，脚本仍会用消息层的逻辑 ID
    /// （例如 `1.80.mw.adv_adv`）组织它，并通过隐藏 `1.80` 一并隐藏消息窗文字。
    /// 这类消息层的逻辑叶节点可能没有物化在场景树中，因此这里不要求目标存在，
    /// 只检查根属性和路径上已经存在的祖先。
    pub fn existing_path_is_visible(&self, id: &str) -> bool {
        if !self.root_props.is_visible() {
            return false;
        }
        let mut current = Some(id);
        while let Some(node_id) = current {
            // 未物化的中间祖先按"未显式隐藏"处理，继续上溯。
            if let Some(layer) = self.nodes.get(node_id)
                && layer.props.visible == Some(false)
            {
                return false;
            }
            current = parent_id(node_id);
        }
        true
    }

    /// 增量合并根图层属性（`[lyprop id="!"]`）。
    pub fn set_root_props(&mut self, raw: &HashMap<String, String>) {
        self.root_props.merge_raw(raw);
    }

    /// 收集以 `id` 为根的整棵子树的所有节点 ID（含自身）。
    pub fn subtree_ids(&self, id: &str) -> Vec<String> {
        let mut ids = Vec::new();
        let mut stack = vec![id.to_string()];
        while let Some(current) = stack.pop() {
            if let Some(node) = self.nodes.get(&current) {
                stack.extend(node.children.iter().cloned());
                ids.push(current);
            }
        }
        ids
    }

    /// 获取指定图层的子图层 ID，按 Artemis 图层顺序排序。
    pub fn children(&self, id: &str) -> Vec<String> {
        self.children_borrowed(id).into_iter().map(str::to_owned).collect()
    }

    /// Compatibility wrapper; the frame builder uses the allocation-free iterator.
    pub fn children_borrowed(&self, id: &str) -> Vec<&str> {
        self.children_ordered(id).collect()
    }

    pub(crate) fn children_ordered(&self, id: &str) -> OrderedIds<'_> {
        let ids = self.get(id).map(|layer| layer.children.as_slice()).unwrap_or(&[]);
        if self.order_cache_disabled { return OrderedIds::uncached(ids); }
        if ids.len() < 2 { return OrderedIds::Direct(ids.iter()); }
        let map = self.traversal_order.children.get_or_init(|| {
            self.nodes.keys().map(|id| (id.clone(), OnceLock::new())).collect()
        });
        let order = map.get(id).expect("all scene nodes have an order entry")
            .get_or_init(|| order::sorted_positions(ids));
        OrderedIds::Indexed { ids, positions: order.iter() }
    }

    /// Diagnostic gate; changing order caching never changes scene identity.
    pub(crate) fn set_order_cache_enabled(&mut self, enabled: bool) {
        if self.order_cache_disabled != !enabled {
            self.order_cache_disabled = !enabled;
            self.traversal_order = TraversalOrder::default();
        }
    }

    pub fn len(&self) -> usize {
        self.nodes.len()
    }

    pub fn is_empty(&self) -> bool {
        self.nodes.is_empty()
    }

    /// 顶层节点 ID，按 Artemis 图层顺序排序（数字优先，数字按值，字符串按字典序）。
    pub fn roots(&self) -> Vec<String> {
        self.roots_borrowed().into_iter().map(str::to_owned).collect()
    }

    pub fn roots_borrowed(&self) -> Vec<&str> {
        self.roots_ordered().collect()
    }

    pub(crate) fn roots_ordered(&self) -> OrderedIds<'_> {
        if self.order_cache_disabled { return OrderedIds::uncached(&self.roots); }
        if self.roots.len() < 2 { return OrderedIds::Direct(self.roots.iter()); }
        let order = self.traversal_order.roots.get_or_init(|| order::sorted_positions(&self.roots));
        OrderedIds::Indexed { ids: &self.roots, positions: order.iter() }
    }

    /// 所有节点的 ID（无序），供需要遍历全树的调用方使用。
    pub fn iter_ids(&self) -> Vec<String> {
        self.nodes.keys().cloned().collect()
    }

    /// 遍历场景中所有图层节点。
    pub fn all_layers(&self) -> impl Iterator<Item = &Layer> {
        self.nodes.values()
    }

    /// 收集当前场景中所有图层引用的纹理文件名称。
    ///
    /// 除图层直接绑定的 `file` 外，还包括：
    /// - file+mask 双图合成纹理的缓存名（否则每帧被 retain 驱逐重建）；
    /// - 单色图层的 1x1 纯色纹理缓存名。
    /// - 中间渲染遮罩与 shader 附加纹理等仅由属性引用的资源。
    pub fn collect_files(&self) -> std::collections::HashSet<String> {
        let mut files = std::collections::HashSet::new();
        for layer in self.nodes.values() {
            if let Some(file) = &layer.file {
                files.insert(file.clone());
                if let Some(mask) = &layer.mask
                    && !file.is_empty()
                {
                    files.insert(crate::render_pipeline::draw::masked_texture_name(
                        file, mask,
                    ));
                    files.insert(mask.clone());
                }
            } else if let Some(rgba) = layer.solid_color {
                files.insert(crate::render_pipeline::draw::solid_texture_name(rgba));
            }

            if let Some(mask) = layer.props.custom.get("intermediate_render_mask")
                && !mask.is_empty()
            {
                files.insert(mask.clone());
            }

            if layer
                .props
                .shader
                .as_deref()
                .is_some_and(|shader| !shader.is_empty())
            {
                if let Some(reference) = layer.props.custom.get("mask") {
                    collect_texture_reference(self, reference, &mut files);
                }
                for slot in &layer.props.shader_textures {
                    if let Some(reference) = layer.props.custom.get(slot) {
                        collect_texture_reference(self, reference, &mut files);
                    }
                }
            }
        }
        files
    }

    /// 确保某 ID 的节点存在（含祖先链接），不改动已有属性。
    pub fn ensure(&mut self, id: &str) {
        self.ensure_path(id);
    }

    /// 创建或替换一个图层。会按需创建缺失的祖先节点（作为纯分组容器），并把它
    /// 登记到父节点的子列表（或根列表）中。若 ID 已存在，则保留其在树中的位置，
    /// 只更新 `file`，属性留给后续 `[lyprop]` 设置。
    ///
    /// 加载了非空 `file` 时同时清除单色模式（二者互斥；蒙版由后续
    /// `set_mask` 重新指定）。
    pub fn create(&mut self, id: &str, file: Option<String>) {
        self.ensure_path(id);
        if let Some(layer) = self.nodes.get_mut(id) {
            if file.as_deref().is_some_and(|f| !f.is_empty()) {
                layer.solid_color = None;
            }
            layer.file = file;
        }
    }

    /// 确保 `id` 及其所有祖先都作为节点存在，并接好父子链接。
    fn ensure_path(&mut self, id: &str) {
        if self.nodes.contains_key(id) {
            return;
        }

        self.traversal_order.insert(id);

        match parent_id(id) {
            Some(parent) => {
                self.ensure_path(parent);
                self.traversal_order.invalidate_children(parent);
                self.nodes
                    .insert(id.to_string(), Layer::new(id.to_string()));
                let parent_node = self
                    .nodes
                    .get_mut(parent)
                    .expect("父节点应已由 ensure_path 创建");
                if !parent_node.children.iter().any(|c| c == id) {
                    parent_node.children.push(id.to_string());
                }
            }
            None => {
                self.nodes
                    .insert(id.to_string(), Layer::new(id.to_string()));
                if !self.roots.iter().any(|r| r == id) {
                    self.traversal_order.roots.take();
                    self.roots.push(id.to_string());
                }
            }
        }
    }

    /// 设置（合并）某图层的属性，会按需创建该节点。增量语义：只改动传入的键。
    pub fn set_props(&mut self, id: &str, raw: &HashMap<String, String>) {
        self.ensure_path(id);
        if let Some(layer) = self.nodes.get_mut(id) {
            layer.props.merge_raw(raw);
        }
    }

    /// 删除一个图层及其整棵子树，并从父节点/根列表中摘除。
    /// 返回被删除的节点数。
    pub fn delete(&mut self, id: &str) -> usize {
        if !self.nodes.contains_key(id) {
            return 0;
        }

        // 先从父节点的子列表（或根列表）里摘除自身。
        match parent_id(id) {
            Some(parent) => {
                self.traversal_order.invalidate_children(parent);
                if let Some(parent_node) = self.nodes.get_mut(parent) {
                    parent_node.children.retain(|c| c != id);
                }
            }
            None => {
                self.traversal_order.roots.take();
                self.roots.retain(|r| r != id);
            }
        }

        self.remove_subtree(id)
    }

    /// 递归移除子树，返回移除的节点数。
    fn remove_subtree(&mut self, id: &str) -> usize {
        self.traversal_order.remove(id);
        let children = match self.nodes.remove(id) {
            Some(node) => node.children,
            None => return 0,
        };
        let mut removed = 1;
        for child in children {
            removed += self.remove_subtree(&child);
        }
        removed
    }

    /// 重命名图层。把节点连同整棵子树搬到新 ID 前缀下，更新父链接。
    /// 新旧 ID 任一非法（如新 ID 已存在）时返回 `false`，不做改动。
    pub fn rename(&mut self, from: &str, to: &str) -> bool {
        if from == to {
            return true;
        }
        if !self.nodes.contains_key(from) || self.nodes.contains_key(to) {
            return false;
        }

        // Rekeying changes both descendants and old/new parent links.
        self.traversal_order = TraversalOrder::default();

        // 从旧父节点摘除。
        match parent_id(from) {
            Some(parent) => {
                if let Some(p) = self.nodes.get_mut(parent) {
                    p.children.retain(|c| c != from);
                }
            }
            None => self.roots.retain(|r| r != from),
        }

        // 递归改键，收集旧→新映射后重建。
        self.rekey_subtree(from, to);

        // 接到新父节点（或根）。
        self.ensure_parent_link(to);
        true
    }

    /// 把以 `from` 为根的子树整体改键到 `to` 前缀下。
    fn rekey_subtree(&mut self, from: &str, to: &str) {
        let mut node = match self.nodes.remove(from) {
            Some(n) => n,
            None => return,
        };
        let children = std::mem::take(&mut node.children);
        node.id = to.to_string();
        let new_children: Vec<String> = children
            .iter()
            .map(|child| {
                // 子 ID 形如 `from.suffix`，替换前缀。
                let suffix = &child[from.len()..];
                format!("{to}{suffix}")
            })
            .collect();
        node.children = new_children;
        self.nodes.insert(to.to_string(), node);

        for child in children {
            let suffix = &child[from.len()..];
            let new_child = format!("{to}{suffix}");
            self.rekey_subtree(&child, &new_child);
        }
    }

    /// 仅为已存在的节点补上父链接（rename 收尾用，不创建祖先）。
    fn ensure_parent_link(&mut self, id: &str) {
        match parent_id(id) {
            Some(parent) => {
                if let Some(p) = self.nodes.get_mut(parent) {
                    if !p.children.iter().any(|c| c == id) {
                        p.children.push(id.to_string());
                    }
                } else {
                    // 父节点不存在时，退化为根，避免悬挂。
                    if !self.roots.iter().any(|r| r == id) {
                        self.roots.push(id.to_string());
                    }
                }
            }
            None => {
                if !self.roots.iter().any(|r| r == id) {
                    self.roots.push(id.to_string());
                }
            }
        }
    }
}

fn collect_texture_reference(
    scene: &Scene,
    reference: &str,
    files: &mut std::collections::HashSet<String>,
) {
    if reference.is_empty() {
        return;
    }
    let file = scene
        .get(reference)
        .and_then(|layer| layer.file.as_deref())
        .unwrap_or(reference);
    if !file.is_empty() {
        files.insert(file.to_string());
    }
}

/// 求点分 ID 的父 ID：`"1.0.-1"` → `"1.0"`，`"1"` → `None`。
fn parent_id(id: &str) -> Option<&str> {
    id.rfind('.').map(|pos| &id[..pos])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raw(pairs: &[(&str, &str)]) -> HashMap<String, String> {
        pairs
            .iter()
            .map(|(k, v)| (k.to_string(), v.to_string()))
            .collect()
    }

    #[test]
    fn create_auto_builds_ancestors() {
        let mut scene = Scene::new();
        scene.create("1.0.-1", Some("black".into()));
        // 祖先 "1" 与 "1.0" 应作为分组节点自动出现。
        assert!(scene.get("1").is_some());
        assert!(scene.get("1.0").is_some());
        assert_eq!(scene.get("1.0.-1").unwrap().file.as_deref(), Some("black"));
        assert_eq!(scene.roots(), &["1".to_string()]);
        assert_eq!(scene.get("1").unwrap().children, vec!["1.0".to_string()]);
    }

    #[test]
    fn delete_removes_whole_subtree() {
        let mut scene = Scene::new();
        scene.create("1.0", Some("a".into()));
        scene.create("1.0.0", Some("b".into()));
        scene.create("1.0.1", Some("c".into()));
        scene.create("2", Some("d".into()));

        let removed = scene.delete("1");
        assert_eq!(removed, 4); // 1, 1.0, 1.0.0, 1.0.1
        assert!(scene.get("1").is_none());
        assert!(scene.get("1.0.0").is_none());
        assert!(scene.get("2").is_some()); // 兄弟子树不受影响
        assert_eq!(scene.roots(), &["2".to_string()]);
    }

    #[test]
    fn set_props_is_incremental_and_autovivifies() {
        let mut scene = Scene::new();
        scene.set_props("5", &raw(&[("left", "10"), ("alpha", "255")]));
        scene.set_props("5", &raw(&[("alpha", "0")]));
        let p = &scene.get("5").unwrap().props;
        assert_eq!(p.left, Some(10.0));
        assert_eq!(p.alpha, Some(0));
    }

    #[test]
    fn insertion_order_preserved_for_children() {
        let mut scene = Scene::new();
        scene.create("1.2", Some("a".into()));
        scene.create("1.0", Some("b".into()));
        scene.create("1.1", Some("c".into()));
        // 子节点按创建顺序排列，而非数值/字典序。
        assert_eq!(
            scene.get("1").unwrap().children,
            vec!["1.2".to_string(), "1.0".to_string(), "1.1".to_string()]
        );
    }

    #[test]
    fn borrowed_traversal_preserves_numeric_overlay_and_stable_equal_order() {
        let mut scene = Scene::new();
        for id in ["1.10", "1.2", "1.-1", "1.01", "1.1", "1.name", "1.9223372036854775808"] {
            scene.ensure(id);
        }
        assert_eq!(scene.children_borrowed("1"), [
            "1.-1", "1.01", "1.1", "1.2", "1.10", "1.9223372036854775808", "1.name"
        ]);
        // Direct public mutation must be reflected without a stale ordering cache.
        scene.get_mut("1").unwrap().children.swap(3, 4);
        assert_eq!(&scene.children_borrowed("1")[1..3], ["1.1", "1.01"]);
        assert!(scene.children_borrowed("missing").is_empty());
        scene.ensure("@art3m1s-message-test");
        scene.ensure("z");
        scene.ensure("-1");
        let saved: Scene = serde_json::from_str(&serde_json::to_string(&scene).unwrap()).unwrap();
        assert_eq!(saved.roots_borrowed(), ["-1", "1", "z", "@art3m1s-message-test"]);
        assert_eq!(saved.children_borrowed("1"), scene.children_borrowed("1"));
        assert_eq!(compare_layer_id("1.2", "1.2.0"), Ordering::Less);
        assert_eq!(compare_layer_id("1.+2", "1.02"), Ordering::Equal);
    }

    #[test]
    fn rename_moves_subtree() {
        let mut scene = Scene::new();
        scene.create("1.0", Some("a".into()));
        scene.create("1.0.0", Some("b".into()));
        assert!(scene.rename("1.0", "1.9"));

        assert!(scene.get("1.0").is_none());
        assert!(scene.get("1.0.0").is_none());
        assert_eq!(scene.get("1.9").unwrap().file.as_deref(), Some("a"));
        assert_eq!(scene.get("1.9.0").unwrap().file.as_deref(), Some("b"));
        assert!(
            scene
                .get("1")
                .unwrap()
                .children
                .contains(&"1.9".to_string())
        );
    }

    #[test]
    fn borrowed_order_matches_after_mutations_snapshot_and_save_reload() {
        fn check(scene: &Scene) {
            let mut expected: Vec<_> = scene.roots.iter().map(String::as_str).collect();
            expected.sort_by(|a,b| compare_layer_id(a,b));
            assert_eq!(scene.roots_borrowed(), expected);
            for (id, layer) in &scene.nodes {
                let mut expected: Vec<_> = layer.children.iter().map(String::as_str).collect();
                expected.sort_by(|a,b| compare_layer_id(a,b));
                assert_eq!(scene.children_borrowed(id), expected);
            }
        }
        let mut scene = Scene::new();
        for id in ["1.01", "1.1", "1.+1", "1.10", "1.-5", "2.5", "1.word", "@art3m1s-message-test"] { scene.ensure(id); }
        for i in 0..40 {
            check(&scene);
            scene.get_mut("1").unwrap().children.rotate_left(1);
            let id = format!("1.{}", 100-i);
            scene.ensure(&id);
            check(&scene);
            scene.delete(&id);
            check(&scene);
            assert!(scene.rename("2", "3")); check(&scene);
            assert!(scene.rename("3", "2")); check(&scene);
            check(&scene.render_snapshot());
            let json = serde_json::to_string(&scene).unwrap();
            assert!(!json.contains("traversal_order"));
            let loaded: Scene = serde_json::from_str(&json).unwrap();
            check(&loaded);
            assert_eq!(serde_json::to_string(&loaded.root_props).unwrap(), serde_json::to_string(&scene.root_props).unwrap());
        }
    }

    #[test]
    #[ignore = "manual host microbenchmark; does not measure Vita frame rate"]
    fn benchmark_cached_scene_order() {
        use std::hint::black_box;
        use std::time::Instant;
        let mut scene = Scene::new();
        for i in 0..64 { scene.ensure(&format!("1.2.{}", (i*37)%64)); }
        let live = &scene.get("1.2").unwrap().children;
        scene.children_borrowed("1.2");
        let rounds = 20000;
        let begin = Instant::now();
        for _ in 0..rounds {
            let mut sorted: Vec<_> = live.iter().map(String::as_str).collect();
            sorted.sort_by(|a,b| compare_layer_id(a,b));
            black_box(sorted);
        }
        let baseline = begin.elapsed();
        let begin = Instant::now();
        for _ in 0..rounds { black_box(scene.children_borrowed(black_box("1.2"))); }
        let cached = begin.elapsed();
        println!("ORDER_BENCH rounds={rounds} siblings=64 uncached_us={} cached_us={} ratio={:.2}", baseline.as_micros(), cached.as_micros(), baseline.as_secs_f64()/cached.as_secs_f64());
    }

    #[test]
    fn order_cache_is_local_bounded_and_never_serialized() {
        let mut scene = Scene::new();
        for id in ["1.10", "1.01", "1.1", "2.10", "2.2"] { scene.ensure(id); }
        let before = serde_json::to_value(&scene).unwrap();
        scene.children_borrowed("1");
        scene.children_borrowed("2");
        scene.roots_borrowed();
        assert_eq!(serde_json::to_value(&scene).unwrap(), before);
        let order2 = scene.traversal_order.children.get().unwrap()["2"].get().unwrap().as_ptr();
        scene.set_props("1", &raw(&[("rotate", "37")]));
        assert!(scene.traversal_order.children.get().unwrap()["1"].get().is_some());
        scene.get_mut("1").unwrap().children.reverse();
        assert!(scene.traversal_order.children.get().unwrap()["1"].get().is_none());
        assert_eq!(scene.traversal_order.children.get().unwrap()["2"].get().unwrap().as_ptr(), order2);
        assert_eq!(scene.children_borrowed("1"), ["1.1", "1.01", "1.10"]);
        scene.ensure("new.deep.7");
        scene.ensure("new.deep.2");
        assert_eq!(scene.children_borrowed("new.deep"), ["new.deep.2", "new.deep.7"]);
        scene.delete("new");
        assert_eq!(scene.traversal_order.children.get().unwrap().len(), scene.len());
        let original: Vec<_> = scene.roots_ordered().map(str::to_owned).collect();
        scene.set_order_cache_enabled(false);
        assert!(scene.traversal_order.children.get().is_none());
        assert_eq!(scene.roots_borrowed(), original);
        assert!(scene.traversal_order.roots.get().is_none());
        scene.set_order_cache_enabled(true);
        assert_eq!(scene.roots_borrowed(), original);
        let cloned = scene.clone();
        assert_eq!(cloned.children_borrowed("1"), scene.children_borrowed("1"));
        scene.replace_with(serde_json::from_value(before).unwrap());
        assert_eq!(scene.children_borrowed("1"), ["1.01", "1.1", "1.10"]);
    }

    #[test]
    fn collect_files_includes_render_only_texture_references() {
        let mut scene = Scene::new();
        scene.create("1", Some("fg".into()));
        scene.set_mask("1", Some("m".into()));
        scene.ensure("2");
        scene.set_solid_color("2", Some([1, 2, 3, 4]));
        scene.ensure("3");
        scene.set_props(
            "3",
            &HashMap::from([
                ("intermediate_render_mask".into(), "face-mask".into()),
                ("shader".into(), "effect".into()),
                ("shadertexture".into(), "textureUser".into()),
                ("textureUser".into(), "shader-user".into()),
                ("mask".into(), "shader-mask".into()),
            ]),
        );

        let files = scene.collect_files();
        assert!(files.contains("fg"));
        assert!(files.contains("m"));
        assert!(
            files.contains(&crate::render_pipeline::draw::masked_texture_name(
                "fg", "m"
            ))
        );
        assert!(
            files.contains(&crate::render_pipeline::draw::solid_texture_name([
                1, 2, 3, 4
            ]))
        );
        assert!(files.contains("face-mask"));
        assert!(files.contains("shader-user"));
        assert!(files.contains("shader-mask"));
    }

    #[test]
    fn create_with_file_clears_solid_mode() {
        let mut scene = Scene::new();
        scene.ensure("1");
        scene.set_solid_color("1", Some([9, 9, 9, 9]));
        scene.create("1", Some("real".into()));
        let layer = scene.get("1").unwrap();
        assert_eq!(layer.solid_color, None);
        assert_eq!(layer.file.as_deref(), Some("real"));
    }

    #[test]
    fn scene_with_root_props_roundtrips_and_legacy_deserializes() {
        let mut scene = Scene::new();
        scene.set_root_props(&raw(&[("alpha", "128")]));
        let json = serde_json::to_string(&scene).unwrap();
        let back: Scene = serde_json::from_str(&json).unwrap();
        assert_eq!(back.root_props().alpha, Some(128));

        // 旧存档（无 root_props / solid_color / mask 字段）仍可反序列化。
        let legacy = r#"{"nodes":{},"roots":[]}"#;
        let scene: Scene = serde_json::from_str(legacy).unwrap();
        assert!(scene.root_props().alpha.is_none());
    }

    #[test]
    fn rename_rejects_existing_target() {
        let mut scene = Scene::new();
        scene.create("1.0", Some("a".into()));
        scene.create("1.1", Some("b".into()));
        assert!(!scene.rename("1.0", "1.1"));
        // 原节点保持不变。
        assert_eq!(scene.get("1.0").unwrap().file.as_deref(), Some("a"));
    }

    #[test]
    fn effective_visibility_is_ancestor_aware() {
        let mut scene = Scene::new();
        // 消息窗 mw（1.mw）下挂文本层（1.mw.adv）。
        scene.create("1.mw", Some("mw".into()));
        scene.create("1.mw.adv", Some("adv".into()));

        // 默认可见。
        assert!(scene.is_effectively_visible("1.mw.adv"));

        // 隐藏 mw（父层 visible=0）→ 文本层有效不可见（右键关闭消息窗的情形）。
        scene.set_props("1.mw", &HashMap::from([("visible".into(), "0".into())]));
        assert!(!scene.is_effectively_visible("1.mw.adv"));
        assert!(!scene.is_effectively_visible("1.mw"));

        // 恢复 mw → 文本层重新可见。
        scene.set_props("1.mw", &HashMap::from([("visible".into(), "1".into())]));
        assert!(scene.is_effectively_visible("1.mw.adv"));

        // 文本层自身隐藏也算不可见。
        scene.set_props("1.mw.adv", &HashMap::from([("visible".into(), "0".into())]));
        assert!(!scene.is_effectively_visible("1.mw.adv"));

        // 根图层（!）隐藏 → 一切不可见。
        scene.set_props("1.mw.adv", &HashMap::from([("visible".into(), "1".into())]));
        scene.set_root_props(&HashMap::from([("visible".into(), "0".into())]));
        assert!(!scene.is_effectively_visible("1.mw.adv"));

        // 不存在的图层视为不可见。
        scene.set_root_props(&HashMap::from([("visible".into(), "1".into())]));
        assert!(!scene.is_effectively_visible("9.9.9"));
    }

    #[test]
    fn logical_path_visibility_checks_existing_ancestors_without_requiring_leaf() {
        let mut scene = Scene::new();
        scene.ensure("1.80");

        assert!(scene.existing_path_is_visible("1.80.mw.adv_adv"));
        scene.set_props("1.80", &HashMap::from([("visible".into(), "0".into())]));
        assert!(!scene.existing_path_is_visible("1.80.mw.adv_adv"));
        assert!(!scene.is_effectively_visible("1.80.mw.adv_adv"));
    }

    #[test]
    fn independent_message_layers_sort_after_image_layers() {
        let mut scene = Scene::new();
        scene.ensure("@art3m1s-message-616476");
        scene.ensure("zzzz");
        scene.ensure("999");

        assert_eq!(
            scene.roots(),
            vec![
                "999".to_string(),
                "zzzz".to_string(),
                "@art3m1s-message-616476".to_string()
            ]
        );
    }
}
