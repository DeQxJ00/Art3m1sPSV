//! Cached positions into the scene-owned insertion-order vectors. No layer ID
//! is normalized, and no cached reference outlives a borrow of the scene.
use std::collections::HashMap;
use std::sync::OnceLock;

#[derive(Debug, Clone, Default)]
pub(super) struct TraversalOrder {
    pub roots: OnceLock<Vec<usize>>,
    pub children: OnceLock<HashMap<String, OnceLock<Vec<usize>>>>,
}

impl TraversalOrder {
    pub fn invalidate_children(&mut self, id: &str) {
        if let Some(map) = self.children.get_mut()
            && let Some(order) = map.get_mut(id)
        {
            order.take();
        }
    }

    pub fn insert(&mut self, id: &str) {
        if let Some(map) = self.children.get_mut() {
            map.insert(id.to_owned(), OnceLock::new());
        }
    }

    pub fn remove(&mut self, id: &str) {
        if let Some(map) = self.children.get_mut() {
            map.remove(id);
        }
    }
}

pub(super) fn sorted_positions(ids: &[String]) -> Vec<usize> {
    let mut order: Vec<_> = (0..ids.len()).collect();
    // Stable sorting preserves insertion order for numeric aliases (01, 1).
    order.sort_by(|&a, &b| super::compare_layer_id(&ids[a], &ids[b]));
    order
}

pub(crate) enum OrderedIds<'a> {
    Direct(std::slice::Iter<'a, String>),
    Indexed {
        ids: &'a [String],
        positions: std::slice::Iter<'a, usize>,
    },
    Uncached(std::vec::IntoIter<&'a str>),
}

impl<'a> OrderedIds<'a> {
    pub(super) fn uncached(ids: &'a [String]) -> Self {
        let mut sorted: Vec<_> = ids.iter().map(String::as_str).collect();
        sorted.sort_by(|a, b| super::compare_layer_id(a, b));
        Self::Uncached(sorted.into_iter())
    }
}

impl<'a> Iterator for OrderedIds<'a> {
    type Item = &'a str;

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            Self::Direct(iter) => iter.next().map(String::as_str),
            Self::Indexed { ids, positions } => positions.next().map(|&i| ids[i].as_str()),
            Self::Uncached(iter) => iter.next(),
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = match self {
            Self::Direct(iter) => iter.len(),
            Self::Indexed { positions, .. } => positions.len(),
            Self::Uncached(iter) => iter.len(),
        };
        (remaining, Some(remaining))
    }
}

impl ExactSizeIterator for OrderedIds<'_> {}
impl std::iter::FusedIterator for OrderedIds<'_> {}
