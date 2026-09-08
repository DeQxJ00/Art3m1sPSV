//! Cache traversal order, while validating public child vectors on every read.
//! Render results always borrow the live IDs; cached copies are only cache keys.
use std::cmp::Ordering;
use std::collections::HashMap;
use std::sync::Mutex;

#[derive(Debug, Default)]
struct Order {
    source: Vec<String>,
    indices: Vec<usize>,
}

impl Order {
    fn sorted<'a>(&mut self, ids: &'a [String], compare: fn(&str, &str) -> Ordering) -> Vec<&'a str> {
        if self.source != ids {
            self.source = ids.to_vec();
            self.indices.clear();
            self.indices.extend(0..ids.len());
            self.indices.sort_by(|&a, &b| compare(&ids[a], &ids[b]));
        }
        self.indices.iter().map(|&i| ids[i].as_str()).collect()
    }
}

#[derive(Debug, Default)]
pub(super) struct TraversalOrder {
    roots: Mutex<Order>,
    children: Mutex<HashMap<String, Order>>,
}

// Cache is expendable and must not inflate scene snapshots or saved state.
impl Clone for TraversalOrder {
    fn clone(&self) -> Self { Self::default() }
}

impl TraversalOrder {
    pub fn sorted<'a>(&self, parent: Option<&str>, ids: &'a [String], compare: fn(&str, &str) -> Ordering) -> Vec<&'a str> {
        if ids.len() < 2 { return ids.iter().map(String::as_str).collect(); }
        if let Some(parent) = parent {
            let mut cache = self.children.lock().unwrap();
            if let Some(order) = cache.get_mut(parent) { return order.sorted(ids, compare); }
            cache.entry(parent.to_owned()).or_default().sorted(ids, compare)
        } else {
            self.roots.lock().unwrap().sorted(ids, compare)
        }
    }

    pub fn discard(&mut self, id: &str) {
        self.children.get_mut().unwrap().remove(id);
    }
}
